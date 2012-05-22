/*
 *  R : A Computer Language for Statistical Data Analysis
 *  Copyright (C) 1999-2012  The R Core Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  http://www.r-project.org/Licenses/
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <Defn.h>
#include <R_ext/Applic.h>

static SEXP getListElement(SEXP list, char *str)
{
    SEXP elmt = R_NilValue, names = getAttrib(list, R_NamesSymbol);
    int i;

    for (i = 0; i < length(list); i++)
	if (strcmp(CHAR(STRING_ELT(names, i)), str) == 0) {
	    elmt = VECTOR_ELT(list, i);
	    break;
	}
    return elmt;
}

static double * vect(int n)
{
    return (double *)R_alloc(n, sizeof(double));
}

typedef struct opt_struct
{
    SEXP R_fcall;    /* function */
    SEXP R_gcall;    /* gradient */
    SEXP R_env;      /* where to evaluate the calls */
    double* ndeps;   /* tolerances for numerical derivatives */
    double fnscale;  /* scaling for objective */
    double* parscale;/* scaling for parameters */
    int usebounds;
    double* lower, *upper;
    SEXP names;	     /* names for par */
} opt_struct, *OptStruct;



static double fminfn(int n, double *p, void *ex)
{
    SEXP s, x;
    int i;
    double val;
    OptStruct OS = (OptStruct) ex;
    PROTECT_INDEX ipx;

    PROTECT(x = allocVector(REALSXP, n));
    if(!isNull(OS->names)) setAttrib(x, R_NamesSymbol, OS->names);
    for (i = 0; i < n; i++) {
	if (!R_FINITE(p[i])) error(_("non-finite value supplied by optim"));
	REAL(x)[i] = p[i] * (OS->parscale[i]);
    }
    SETCADR(OS->R_fcall, x);
    PROTECT_WITH_INDEX(s = eval(OS->R_fcall, OS->R_env), &ipx);
    REPROTECT(s = coerceVector(s, REALSXP), ipx);
    if (LENGTH(s) != 1)
	error(_("objective function in optim evaluates to length %d not 1"),
	      LENGTH(s));
    val = REAL(s)[0]/(OS->fnscale);
    UNPROTECT(2);
    return val;
}

static void fmingr(int n, double *p, double *df, void *ex)
{
    SEXP s, x;
    int i;
    double val1, val2, eps, epsused, tmp;
    OptStruct OS = (OptStruct) ex;
    PROTECT_INDEX ipx;

    if (!isNull(OS->R_gcall)) { /* analytical derivatives */
	PROTECT(x = allocVector(REALSXP, n));
	if(!isNull(OS->names)) setAttrib(x, R_NamesSymbol, OS->names);
	for (i = 0; i < n; i++) {
	    if (!R_FINITE(p[i]))
		error(_("non-finite value supplied by optim"));
	    REAL(x)[i] = p[i] * (OS->parscale[i]);
	}
	SETCADR(OS->R_gcall, x);
	PROTECT_WITH_INDEX(s = eval(OS->R_gcall, OS->R_env), &ipx);
	REPROTECT(s = coerceVector(s, REALSXP), ipx);
	if(LENGTH(s) != n)
	    error(_("gradient in optim evaluated to length %d not %d"),
		  LENGTH(s), n);
	for (i = 0; i < n; i++)
	    df[i] = REAL(s)[i] * (OS->parscale[i])/(OS->fnscale);
	UNPROTECT(2);
    } else { /* numerical derivatives */
	PROTECT(x = allocVector(REALSXP, n));
	setAttrib(x, R_NamesSymbol, OS->names);
	for (i = 0; i < n; i++) REAL(x)[i] = p[i] * (OS->parscale[i]);
	SETCADR(OS->R_fcall, x);
	if(OS->usebounds == 0) {
	    for (i = 0; i < n; i++) {
		eps = OS->ndeps[i];
		REAL(x)[i] = (p[i] + eps) * (OS->parscale[i]);
		SETCADR(OS->R_fcall, x);
		PROTECT_WITH_INDEX(s = eval(OS->R_fcall, OS->R_env), &ipx);
		REPROTECT(s = coerceVector(s, REALSXP), ipx);
		val1 = REAL(s)[0]/(OS->fnscale);
		REAL(x)[i] = (p[i] - eps) * (OS->parscale[i]);
		SETCADR(OS->R_fcall, x);
		REPROTECT(s = eval(OS->R_fcall, OS->R_env), ipx);
		REPROTECT(s = coerceVector(s, REALSXP), ipx);
		val2 = REAL(s)[0]/(OS->fnscale);
		df[i] = (val1 - val2)/(2 * eps);
#define DO_df_x							\
		if(!R_FINITE(df[i]))					\
		    error(("non-finite finite-difference value [%d]"), i+1);\
		REAL(x)[i] = p[i] * (OS->parscale[i])

		DO_df_x;
		UNPROTECT(1);
	    }
	} else { /* usebounds */
	    for (i = 0; i < n; i++) {
		epsused = eps = OS->ndeps[i];
		tmp = p[i] + eps;
		if (tmp > OS->upper[i]) {
		    tmp = OS->upper[i];
		    epsused = tmp - p[i] ;
		}
		REAL(x)[i] = tmp * (OS->parscale[i]);
		SETCADR(OS->R_fcall, x);
		PROTECT_WITH_INDEX(s = eval(OS->R_fcall, OS->R_env), &ipx);
		REPROTECT(s = coerceVector(s, REALSXP), ipx);
		val1 = REAL(s)[0]/(OS->fnscale);
		tmp = p[i] - eps;
		if (tmp < OS->lower[i]) {
		    tmp = OS->lower[i];
		    eps = p[i] - tmp;
		}
		REAL(x)[i] = tmp * (OS->parscale[i]);
		SETCADR(OS->R_fcall, x);
		REPROTECT(s = eval(OS->R_fcall, OS->R_env), ipx);
		REPROTECT(s = coerceVector(s, REALSXP), ipx);
		val2 = REAL(s)[0]/(OS->fnscale);
		df[i] = (val1 - val2)/(epsused + eps);

		DO_df_x;
		UNPROTECT(1);
	    }
	}
	UNPROTECT(1); /* x */
    }
}

/* par fn gr method options */
SEXP attribute_hidden do_optim(SEXP call, SEXP op, SEXP args, SEXP rho)
{
    SEXP par, fn, gr, method, options, tmp, slower, supper;
    SEXP res, value, counts, conv;
    int i, npar=0, *mask, trace, maxit, fncount, grcount, nREPORT, tmax;
    int ifail = 0;
    double *dpar, *opar, val, abstol, reltol, temp;
    const char *tn;
    OptStruct OS;

    checkArity(op, args);
    OS = (OptStruct) R_alloc(1, sizeof(opt_struct));
    OS->usebounds = 0;
    OS->R_env = rho;
    par = CAR(args);
    OS->names = getAttrib(par, R_NamesSymbol);
    args = CDR(args); fn = CAR(args);
    if (!isFunction(fn)) error(_("'fn' is not a function"));
    args = CDR(args); gr = CAR(args);
    args = CDR(args); method = CAR(args);
    if (!isString(method)|| LENGTH(method) != 1)
	error(_("invalid '%s' argument"), "method");
    tn = CHAR(STRING_ELT(method, 0));
    args = CDR(args); options = CAR(args);
    PROTECT(OS->R_fcall = lang2(fn, R_NilValue));
    /* I don't think duplication is needed here */
    PROTECT(par = coerceVector(duplicate(par), REALSXP));
    npar = LENGTH(par);
    dpar = vect(npar);
    opar = vect(npar);
    trace = asInteger(getListElement(options, "trace"));
    OS->fnscale = asReal(getListElement(options, "fnscale"));
    tmp = getListElement(options, "parscale");
    if (LENGTH(tmp) != npar)
	error(_("'parscale' is of the wrong length"));
    PROTECT(tmp = coerceVector(tmp, REALSXP));
    OS->parscale = vect(npar);
    for (i = 0; i < npar; i++) OS->parscale[i] = REAL(tmp)[i];
    UNPROTECT(1);
    for (i = 0; i < npar; i++)
	dpar[i] = REAL(par)[i] / (OS->parscale[i]);
    PROTECT(res = allocVector(VECSXP, 5));
    PROTECT(value = allocVector(REALSXP, 1));
    PROTECT(counts = allocVector(INTSXP, 2));
    PROTECT(conv = allocVector(INTSXP, 1));
    abstol = asReal(getListElement(options, "abstol"));
    reltol = asReal(getListElement(options, "reltol"));
    maxit = asInteger(getListElement(options, "maxit"));
    if (maxit == NA_INTEGER) error(_("'maxit' is not an integer"));

    if (strcmp(tn, "Nelder-Mead") == 0) {
	double alpha, beta, gamm;

	alpha = asReal(getListElement(options, "alpha"));
	beta = asReal(getListElement(options, "beta"));
	gamm = asReal(getListElement(options, "gamma"));
	nmmin(npar, dpar, opar, &val, fminfn, &ifail, abstol, reltol,
	      (void *)OS, alpha, beta, gamm, trace, &fncount, maxit);
	for (i = 0; i < npar; i++)
	    REAL(par)[i] = opar[i] * (OS->parscale[i]);
	grcount = NA_INTEGER;

    }
    else if (strcmp(tn, "SANN") == 0) {
	tmax = asInteger(getListElement(options, "tmax"));
	temp = asReal(getListElement(options, "temp"));
	if (trace) trace = asInteger(getListElement(options, "REPORT"));
	if (tmax == NA_INTEGER) error(_("'tmax' is not an integer"));
	if (!isNull(gr)) {
	    if (!isFunction(gr)) error(_("'gr' is not a function"));
		PROTECT(OS->R_gcall = lang2(gr, R_NilValue));
	} else {
	    PROTECT(OS->R_gcall = R_NilValue); /* for balance */
	}
	samin (npar, dpar, &val, fminfn, maxit, tmax, temp, trace, (void *)OS);
	for (i = 0; i < npar; i++)
	    REAL(par)[i] = dpar[i] * (OS->parscale[i]);
	fncount = npar > 0 ? maxit : 1;
	grcount = NA_INTEGER;
	UNPROTECT(1);  /* OS->R_gcall */

    } else if (strcmp(tn, "BFGS") == 0) {
	SEXP ndeps;

	nREPORT = asInteger(getListElement(options, "REPORT"));
	if (!isNull(gr)) {
	    if (!isFunction(gr)) error(_("'gr' is not a function"));
	    PROTECT(OS->R_gcall = lang2(gr, R_NilValue));
	} else {
	    PROTECT(OS->R_gcall = R_NilValue); /* for balance */
	    ndeps = getListElement(options, "ndeps");
	    if (LENGTH(ndeps) != npar)
		error(_("'ndeps' is of the wrong length"));
	    OS->ndeps = vect(npar);
	    PROTECT(ndeps = coerceVector(ndeps, REALSXP));
	    for (i = 0; i < npar; i++) OS->ndeps[i] = REAL(ndeps)[i];
	    UNPROTECT(1);
	}
	mask = (int *) R_alloc(npar, sizeof(int));
	for (i = 0; i < npar; i++) mask[i] = 1;
	vmmin(npar, dpar, &val, fminfn, fmingr, maxit, trace, mask, abstol,
	      reltol, nREPORT, (void *)OS, &fncount, &grcount, &ifail);
	for (i = 0; i < npar; i++)
	    REAL(par)[i] = dpar[i] * (OS->parscale[i]);
	UNPROTECT(1); /* OS->R_gcall */
    } else if (strcmp(tn, "CG") == 0) {
	int type;
	SEXP ndeps;

	type = asInteger(getListElement(options, "type"));
	if (!isNull(gr)) {
	    if (!isFunction(gr)) error(_("'gr' is not a function"));
	    PROTECT(OS->R_gcall = lang2(gr, R_NilValue));
	} else {
	    PROTECT(OS->R_gcall = R_NilValue); /* for balance */
	    ndeps = getListElement(options, "ndeps");
	    if (LENGTH(ndeps) != npar)
		error(_("'ndeps' is of the wrong length"));
	    OS->ndeps = vect(npar);
	    PROTECT(ndeps = coerceVector(ndeps, REALSXP));
	    for (i = 0; i < npar; i++) OS->ndeps[i] = REAL(ndeps)[i];
	    UNPROTECT(1);
	}
	cgmin(npar, dpar, opar, &val, fminfn, fmingr, &ifail, abstol,
	      reltol, (void *)OS, type, trace, &fncount, &grcount, maxit);
	for (i = 0; i < npar; i++)
	    REAL(par)[i] = opar[i] * (OS->parscale[i]);
	UNPROTECT(1); /* OS->R_gcall */

    } else if (strcmp(tn, "L-BFGS-B") == 0) {
	SEXP ndeps, smsg;
	double *lower = vect(npar), *upper = vect(npar);
	int lmm, *nbd = (int *) R_alloc(npar, sizeof(int));
	double factr, pgtol;
	char msg[60];

	nREPORT = asInteger(getListElement(options, "REPORT"));
	factr = asReal(getListElement(options, "factr"));
	pgtol = asReal(getListElement(options, "pgtol"));
	lmm = asInteger(getListElement(options, "lmm"));
	if (!isNull(gr)) {
	    if (!isFunction(gr)) error(_("'gr' is not a function"));
	    PROTECT(OS->R_gcall = lang2(gr, R_NilValue));
	} else {
	    PROTECT(OS->R_gcall = R_NilValue); /* for balance */
	    ndeps = getListElement(options, "ndeps");
	    if (LENGTH(ndeps) != npar)
		error(_("'ndeps' is of the wrong length"));
	    OS->ndeps = vect(npar);
	    PROTECT(ndeps = coerceVector(ndeps, REALSXP));
	    for (i = 0; i < npar; i++) OS->ndeps[i] = REAL(ndeps)[i];
	    UNPROTECT(1);
	}
	args = CDR(args); slower = CAR(args); /* coerce in calling code */
	args = CDR(args); supper = CAR(args);
	for (i = 0; i < npar; i++) {
	    lower[i] = REAL(slower)[i] / (OS->parscale[i]);
	    upper[i] = REAL(supper)[i] / (OS->parscale[i]);
	    if (!R_FINITE(lower[i])) {
		if (!R_FINITE(upper[i])) nbd[i] = 0; else nbd[i] = 3;
	    } else {
		if (!R_FINITE(upper[i])) nbd[i] = 1; else nbd[i] = 2;
	    }
	}
	OS->usebounds = 1;
	OS->lower = lower;
	OS->upper = upper;
	lbfgsb(npar, lmm, dpar, lower, upper, nbd, &val, fminfn, fmingr,
	       &ifail, (void *)OS, factr, pgtol, &fncount, &grcount,
	       maxit, msg, trace, nREPORT);
	for (i = 0; i < npar; i++)
	    REAL(par)[i] = dpar[i] * (OS->parscale[i]);
	UNPROTECT(1); /* OS->R_gcall */
	PROTECT(smsg = mkString(msg));
	SET_VECTOR_ELT(res, 4, smsg);
	UNPROTECT(1);
    } else
	error(_("unknown 'method'"));

    REAL(value)[0] = val * (OS->fnscale);
    SET_VECTOR_ELT(res, 0, par); SET_VECTOR_ELT(res, 1, value);
    INTEGER(counts)[0] = fncount; INTEGER(counts)[1] = grcount;
    SET_VECTOR_ELT(res, 2, counts);
    INTEGER(conv)[0] = ifail;
    SET_VECTOR_ELT(res, 3, conv);
    UNPROTECT(6);
    return res;
}

/* par fn gr options */
SEXP attribute_hidden do_optimhess(SEXP call, SEXP op, SEXP args, SEXP rho)
{
    SEXP par, fn, gr, options, tmp, ndeps, ans;
    OptStruct OS;
    int npar, i , j;
    double *dpar, *df1, *df2, eps;

    checkArity(op, args);
    OS = (OptStruct) R_alloc(1, sizeof(opt_struct));
    OS->usebounds = 0;
    OS->R_env = rho;
    par = CAR(args);
    npar = LENGTH(par);
    OS->names = getAttrib(par, R_NamesSymbol);
    args = CDR(args); fn = CAR(args);
    if (!isFunction(fn)) error(_("'fn' is not a function"));
    args = CDR(args); gr = CAR(args);
    args = CDR(args); options = CAR(args);
    OS->fnscale = asReal(getListElement(options, "fnscale"));
    tmp = getListElement(options, "parscale");
    if (LENGTH(tmp) != npar)
	error(_("'parscale' is of the wrong length"));
    PROTECT(tmp = coerceVector(tmp, REALSXP));
    OS->parscale = vect(npar);
    for (i = 0; i < npar; i++) OS->parscale[i] = REAL(tmp)[i];
    UNPROTECT(1);
    PROTECT(OS->R_fcall = lang2(fn, R_NilValue));
    PROTECT(par = coerceVector(par, REALSXP));
    if (!isNull(gr)) {
	if (!isFunction(gr)) error(_("'gr' is not a function"));
	PROTECT(OS->R_gcall = lang2(gr, R_NilValue));
    } else {
	PROTECT(OS->R_gcall = R_NilValue); /* for balance */
    }
    ndeps = getListElement(options, "ndeps");
    if (LENGTH(ndeps) != npar) error(_("'ndeps' is of the wrong length"));
    OS->ndeps = vect(npar);
    PROTECT(ndeps = coerceVector(ndeps, REALSXP));
    for (i = 0; i < npar; i++) OS->ndeps[i] = REAL(ndeps)[i];
    UNPROTECT(1);
    PROTECT(ans = allocMatrix(REALSXP, npar, npar));
    dpar = vect(npar);
    for (i = 0; i < npar; i++)
	dpar[i] = REAL(par)[i] / (OS->parscale[i]);
    df1 = vect(npar);
    df2 = vect(npar);
    for (i = 0; i < npar; i++) {
	eps = OS->ndeps[i]/(OS->parscale[i]);
	dpar[i] = dpar[i] + eps;
	fmingr(npar, dpar, df1, (void *)OS);
	dpar[i] = dpar[i] - 2 * eps;
	fmingr(npar, dpar, df2, (void *)OS);
	for (j = 0; j < npar; j++)
	    REAL(ans)[i * npar + j] = (OS->fnscale) * (df1[j] - df2[j])/
		(2 * eps * (OS->parscale[i]) * (OS->parscale[j]));
	dpar[i] = dpar[i] + eps;
    }
    UNPROTECT(4);
    return ans;
}
