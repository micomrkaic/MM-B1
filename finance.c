/*
 * MM-B1 -- a decimal business RPN calculator.
 *
 * Copyright (C) 2026 Mico Mrkaic
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/* finance.c -- the numerical kernel.
 *
 * Periodic model.  i is the per-period rate in PERCENT; n is the number of
 * periods.  Sign convention: cash received +, cash paid -.  TVM identity
 * (type = 1 when payments fall at the start of the period):
 *
 *     PV*(1+r)^n + PMT*(1+r*type)*[((1+r)^n - 1)/r] + FV = 0,   r = i/100
 */
#include "bc.h"
#include <stdlib.h>

/* ---- small helpers ---------------------------------------------------- */
static void onep(num *out, const num *ipct)      /* out = 1 + ipct/100 */
{
    num h, frac, one; n_int(&h,100); n_int(&one,1);
    n_div(&frac, ipct, &h);
    n_add(out, &one, &frac);
}
static void frac_of(num *out, const num *ipct)   /* out = ipct/100 */
{
    num h; n_int(&h,100); n_div(out, ipct, &h);
}
static int is_smallrate(const num *r)            /* |r| < 1e-20 -> treat as 0 */
{
    num a, eps; n_abs(&a,r); n_str(&eps,"1E-20");
    return n_cmp(&a,&eps) < 0;
}
static long as_long(const num *a)
{
    num t, z; n_int(&z,0);
    decNumberRescale(&t, a, &z, &g_ctx);
    long v = decNumberToInt32(&t, &g_ctx);
    decContextZeroStatus(&g_ctx);
    return v;
}

/* future-value annuity factor A = ((1+r)^n - 1)/r, r->0 limit A = n */
static void ann_factor(num *A, const num *g, const num *rfrac, const num *n)
{
    if (is_smallrate(rfrac)) { n_cpy(A, n); return; }
    num one, gm1; n_int(&one,1);
    n_sub(&gm1, g, &one);
    n_div(A, &gm1, rfrac);
}
/* present-value annuity factor A = (1 - (1+r)^-n)/r = (1 - 1/g)/r, limit n */
static void pv_ann_factor(num *A, const num *g, const num *rfrac, const num *n)
{
    if (is_smallrate(rfrac)) { n_cpy(A, n); return; }
    num one, inv, t; n_int(&one,1);
    n_div(&inv, &one, g);
    n_sub(&t, &one, &inv);
    n_div(A, &t, rfrac);
}
/* k = 1 + r*type */
static void due_factor(num *k, const num *rfrac, int begin)
{
    num one; n_int(&one,1);
    if (!begin) { n_cpy(k,&one); return; }
    n_add(k, &one, rfrac);
}

/* ---- TVM closed forms ------------------------------------------------- */
int fin_tvm_fv(const num *n,const num *ipct,const num *pv,const num *pmt,int begin,num *out)
{
    num r,g,A,k,t1,t2,k_A;
    frac_of(&r,ipct); { num o; onep(&o,ipct); n_pow(&g,&o,n); }
    ann_factor(&A,&g,&r,n); due_factor(&k,&r,begin);
    n_mul(&t1, pv, &g);
    n_mul(&k_A,&k,&A); n_mul(&t2, pmt, &k_A);
    n_add(out,&t1,&t2); n_neg(out,out);
    return dec_check("fv");
}
int fin_tvm_pv(const num *n,const num *ipct,const num *pmt,const num *fv,int begin,num *out)
{
    num r,g,A,k,k_A,t,o;
    frac_of(&r,ipct); onep(&o,ipct); n_pow(&g,&o,n);
    ann_factor(&A,&g,&r,n); due_factor(&k,&r,begin);
    n_mul(&k_A,&k,&A); n_mul(&t,pmt,&k_A);
    n_add(&t,&t,fv); n_neg(&t,&t);
    n_div(out,&t,&g);
    return dec_check("pv");
}
int fin_tvm_pmt(const num *n,const num *ipct,const num *pv,const num *fv,int begin,num *out)
{
    num r,g,A,k,k_A,num_,o;
    frac_of(&r,ipct); onep(&o,ipct); n_pow(&g,&o,n);
    ann_factor(&A,&g,&r,n); due_factor(&k,&r,begin);
    n_mul(&k_A,&k,&A);
    if (n_iszero(&k_A)) { set_err("pmt: degenerate (n=0)"); return 1; }
    n_mul(&num_,pv,&g); n_add(&num_,&num_,fv); n_neg(&num_,&num_);
    n_div(out,&num_,&k_A);
    return dec_check("pmt");
}
int fin_tvm_n(const num *ipct,const num *pv,const num *pmt,const num *fv,int begin,num *out)
{
    num r; frac_of(&r,ipct);
    if (is_smallrate(&r)) {                         /* n = -(pv+fv)/pmt */
        if (n_iszero(pmt)) { set_err("n: pmt is zero"); return 1; }
        num s; n_add(&s,pv,fv); n_neg(&s,&s); n_div(out,&s,pmt);
        return dec_check("n");
    }
    num k,q,onep_,gnum,gden,g,lng,ln1;
    due_factor(&k,&r,begin);
    n_mul(&q,pmt,&k); n_div(&q,&q,&r);              /* q = pmt*k/r */
    n_sub(&gnum,&q,fv);                              /* q - fv */
    n_add(&gden,pv,&q);                              /* pv + q */
    if (n_iszero(&gden)) { set_err("n: no solution"); return 1; }
    n_div(&g,&gnum,&gden);
    if (n_isneg(&g) || n_iszero(&g)) { set_err("n: no solution (non-positive growth)"); return 1; }
    n_ln(&lng,&g); onep(&onep_,ipct); n_ln(&ln1,&onep_);
    n_div(out,&lng,&ln1);
    return dec_check("n");
}

/* ---- residuals + root finder (scan to bracket, decimal bisection) ----- */
struct tvm_args { const num *n,*pv,*pmt,*fv; int begin; };

static int tvm_residual(const num *ipct, num *out, void *ud)
{
    struct tvm_args *a = ud;
    num r,g,A,k,t1,t2,k_A,o;
    frac_of(&r,ipct); onep(&o,ipct); n_pow(&g,&o,a->n);
    ann_factor(&A,&g,&r,a->n); due_factor(&k,&r,a->begin);
    n_mul(&t1,a->pv,&g);
    n_mul(&k_A,&k,&A); n_mul(&t2,a->pmt,&k_A);
    n_add(out,&t1,&t2); n_add(out,out,a->fv);
    return 0;
}
static int npv_residual(const num *ipct, num *out, void *ud)
{
    (void)ud;
    num r,onep_,disc,acc,term,texp; long t = 0;
    frac_of(&r,ipct); { num one; n_int(&one,1); n_add(&onep_,&one,&r); }
    n_int(&acc,0);
    for (int j = 0; j < g_ncf; j++) {
        for (long c = 0; c < g_cf[j].cnt; c++, t++) {
            n_int(&texp,t); n_pow(&disc,&onep_,&texp);  /* (1+r)^t */
            n_div(&term,&g_cf[j].amt,&disc);
            n_add(&acc,&acc,&term);
        }
    }
    n_cpy(out,&acc);
    return 0;
}

typedef int (*resfn)(const num *, num *, void *);

static int sign_of(resfn f, void *ud, const num *x)
{
    num r; f(x,&r,ud);
    if (n_iszero(&r)) return 0;
    return n_isneg(&r) ? -1 : 1;
}
/* scan percent grid for a sign change, then bisect in decimal. */
static int solve_rate(resfn f, void *ud, num *root_pct)
{
    double lo = -99.0, hi = 1000.0, step = 0.25;
    num xa, xb; char buf[32];
    double a = lo;
    snprintf(buf,sizeof buf,"%.6f",a); n_str(&xa,buf);
    int sa = sign_of(f,ud,&xa);
    if (sa == 0) { n_cpy(root_pct,&xa); return 0; }
    int found = 0;
    for (double b = lo + step; b <= hi + 1e-9; b += step) {
        snprintf(buf,sizeof buf,"%.6f",b); n_str(&xb,buf);
        int sb = sign_of(f,ud,&xb);
        if (sb == 0) { n_cpy(root_pct,&xb); return 0; }
        if (sb != sa) { found = 1; break; }
        n_cpy(&xa,&xb); sa = sb; a = b;
    }
    if (!found) { set_err("no rate found in [-99%%, 1000%%]"); return 1; }
    /* decimal bisection on [xa, xb] */
    num mid, half; n_str(&half,"0.5");
    for (int it = 0; it < 200; it++) {
        n_add(&mid,&xa,&xb); n_mul(&mid,&mid,&half);
        int sm = sign_of(f,ud,&mid);
        if (sm == 0) break;
        if (sm == sa) n_cpy(&xa,&mid); else n_cpy(&xb,&mid);
    }
    n_add(root_pct,&xa,&xb); n_mul(root_pct,root_pct,&half);
    return 0;
}

int fin_tvm_i(const num *n,const num *pv,const num *pmt,const num *fv,int begin,num *out)
{
    struct tvm_args a = { n, pv, pmt, fv, begin };
    return solve_rate(tvm_residual, &a, out);
}

/* ---- NPV / IRR -------------------------------------------------------- */
int fin_npv(const num *ipct, num *out)
{
    if (g_ncf == 0) { set_err("no cash flows"); return 1; }
    npv_residual(ipct, out, NULL);
    return dec_check("npv");
}
int fin_irr(num *out)
{
    if (g_ncf < 2) { set_err("need at least an outlay and a return"); return 1; }
    return solve_rate(npv_residual, NULL, out);
}

/* ---- amortization (end-of-period, magnitudes) ------------------------- */
int fin_amort(void)
{
    long N = as_long(&T_n);
    if (N < 1) { set_err("amort: set n >= 1"); return 1; }
    if (n_iszero(&T_pmt)) { set_err("amort: set pmt (or solve ?pmt) first"); return 1; }

    num bal, rate, pay, interest, principal, totI, totP, o;
    n_abs(&bal,&T_pv); frac_of(&rate,&T_i); n_abs(&pay,&T_pmt);
    n_int(&totI,0); n_int(&totP,0);

    char b1[64],b2[64],b3[64],b4[64];
    printf("  per        payment       interest      principal        balance\n");
    printf("  --- -------------- -------------- -------------- --------------\n");
    for (long p = 1; p <= N; p++) {
        n_mul(&interest,&bal,&rate);
        n_sub(&principal,&pay,&interest);
        n_sub(&bal,&bal,&principal);
        n_add(&totI,&totI,&interest);
        n_add(&totP,&totP,&principal);
        fmt_fixed(&pay,g_dp,b1); fmt_fixed(&interest,g_dp,b2);
        fmt_fixed(&principal,g_dp,b3); fmt_fixed(&bal,g_dp,b4);
        printf("  %3ld %14s %14s %14s %14s\n", p, b1, b2, b3, b4);
    }
    fmt_fixed(&totI,g_dp,b2); fmt_fixed(&totP,g_dp,b3);
    printf("  --- -------------- -------------- -------------- --------------\n");
    printf("  tot                %14s %14s\n", b2, b3);
    (void)o;
    return dec_check("amort");
}

/* ---- interest-rate conversions ---------------------------------------- */
int fin_nom2eff(const num *nompct,const num *m,num *out)
{
    num h,one,base,p,t; n_int(&h,100); n_int(&one,1);
    n_div(&base,nompct,&h); n_div(&base,&base,m); n_add(&base,&base,&one);
    n_pow(&p,&base,m); n_sub(&t,&p,&one); n_mul(out,&t,&h);
    return dec_check("nom>eff");
}
int fin_eff2nom(const num *effpct,const num *m,num *out)
{
    num h,one,base,inv,p,t; n_int(&h,100); n_int(&one,1);
    n_div(&base,effpct,&h); n_add(&base,&base,&one);
    n_div(&inv,&one,m); n_pow(&p,&base,&inv); n_sub(&t,&p,&one);
    n_mul(&t,&t,m); n_mul(out,&t,&h);
    return dec_check("eff>nom");
}

/* ---- depreciation ----------------------------------------------------- */
int fin_sl(const num *cost,const num *salv,const num *life,num *out)
{
    num d; n_sub(&d,cost,salv); n_div(out,&d,life);
    return dec_check("sl");
}
int fin_soyd(const num *cost,const num *salv,const num *life,const num *year,num *out)
{
    num base,one,rem,sum,t;
    n_sub(&base,cost,salv); n_int(&one,1);
    n_sub(&rem,life,year); n_add(&rem,&rem,&one);     /* life-year+1 */
    n_add(&sum,life,&one); n_mul(&sum,&sum,life);     /* life*(life+1) */
    { num two; n_int(&two,2); n_div(&sum,&sum,&two); }/* /2 */
    n_mul(&t,&base,&rem); n_div(out,&t,&sum);
    return dec_check("soyd");
}
int fin_db(const num *cost,const num *salv,const num *life,const num *year,const num *factor,num *out)
{
    num one,rate,ym1,bookstart,dep,floor_,o;
    n_int(&one,1);
    n_div(&rate,factor,life);                         /* factor/life */
    n_sub(&ym1,year,&one);
    { num base; n_sub(&base,&one,&rate); n_pow(&o,&base,&ym1); }
    n_mul(&bookstart,cost,&o);                        /* cost*(1-rate)^(year-1) */
    n_mul(&dep,&bookstart,&rate);
    n_sub(&floor_,&bookstart,salv);                   /* cannot depreciate below salvage */
    if (n_isneg(&floor_)) n_int(&floor_,0);
    if (n_cmp(&dep,&floor_) > 0) n_cpy(&dep,&floor_);
    n_cpy(out,&dep);
    return dec_check("db");
}

/* ---- bonds (settlement on a coupon date; no day-count / accrued) ------ */
int fin_bond_price(const num *cpnpct,const num *yldpct,const num *nper,const num *redeem,num *out)
{
    num y,g,A,coupon,t1,t2,o;
    frac_of(&y,yldpct);
    onep(&o,yldpct); n_pow(&g,&o,nper);               /* (1+y)^n */
    pv_ann_factor(&A,&g,&y,nper);
    n_cpy(&coupon,cpnpct);                             /* coupon per 100 face */
    n_mul(&t1,&coupon,&A);
    n_div(&t2,redeem,&g);
    n_add(out,&t1,&t2);
    return dec_check("bondprice");
}
struct bond_args { const num *cpn,*nper,*redeem,*price; };
static int bond_residual(const num *yldpct, num *out, void *ud)
{
    struct bond_args *b = ud;
    num pr; if (fin_bond_price(b->cpn,yldpct,b->nper,b->redeem,&pr)) return 1;
    n_sub(out,&pr,b->price);
    return 0;
}
int fin_bond_yield(const num *price,const num *cpnpct,const num *nper,const num *redeem,num *out)
{
    struct bond_args b = { cpnpct, nper, redeem, price };
    return solve_rate(bond_residual, &b, out);
}

/* ====================================================================== *
 *  Dated functions: XNPV / XIRR and date-aware bond price / yield.
 * ====================================================================== */

/* XNPV: actual/365 discounting from the first flow's date (Excel rule). */
static int xnpv_residual(const num *ratepct, num *out, void *ud)
{
    (void)ud;
    num r, one, base, acc, term, expo, df, daysn, t365;
    frac_of(&r, ratepct);
    n_int(&one,1); n_add(&base,&one,&r);
    n_int(&acc,0); n_int(&t365,365);
    long d0 = g_xcf[0].date;
    for (int i = 0; i < g_nxcf; i++) {
        long days; if (date_days(d0, g_xcf[i].date, &days)) return 1;
        n_int(&daysn, days); n_div(&expo, &daysn, &t365);
        n_pow(&df, &base, &expo);
        n_div(&term, &g_xcf[i].amt, &df);
        n_add(&acc, &acc, &term);
    }
    n_cpy(out, &acc);
    return 0;
}
int fin_xnpv(const num *ratepct, num *out)
{
    if (g_nxcf == 0) { set_err("no dated cash flows"); return 1; }
    if (xnpv_residual(ratepct, out, NULL)) return 1;
    return dec_check("xnpv");
}
int fin_xirr(num *out)
{
    if (g_nxcf < 2) { set_err("need at least an outlay and a return"); return 1; }
    return solve_rate(xnpv_residual, NULL, out);
}

/* SIA / Excel dated bond clean price (per 100 face). */
int fin_xbond_price(long settle,long maturity,const num *ratepct,const num *yldpct,
                    const num *redeem,int freq,int basis,num *out)
{
    int N; long A,E;
    if (coupon_geometry(settle,maturity,freq,basis,&N,&A,&E)) return 1;
    if (E == 0) { set_err("degenerate coupon period"); return 1; }

    num frq,cpn,yper,one,dbase,An,En,DSCn,t1,accr_frac,accrued,clean;
    n_int(&frq,freq); n_int(&one,1);
    n_div(&cpn, ratepct, &frq);              /* coupon per period per 100 face */
    frac_of(&yper, yldpct); n_div(&yper,&yper,&frq);
    n_add(&dbase,&one,&yper);                /* 1 + y/freq */
    n_int(&An,A); n_int(&En,E);
    n_sub(&DSCn,&En,&An); n_div(&t1,&DSCn,&En);   /* DSC/E */
    n_div(&accr_frac,&An,&En);                    /* A/E   */
    n_mul(&accrued,&cpn,&accr_frac);

    if (N == 1) {                            /* final period: simple interest */
        num denom,tmp;
        n_mul(&tmp,&t1,&yper); n_add(&denom,&one,&tmp);
        n_add(&clean,redeem,&cpn); n_div(&clean,&clean,&denom);
        n_sub(&clean,&clean,&accrued);
    } else {
        num sum,kk,expo,df,term,rexpo,rdf,rpv;
        n_int(&sum,0);
        for (int k = 1; k <= N; k++) {
            n_int(&kk,k-1); n_add(&expo,&kk,&t1);
            n_pow(&df,&dbase,&expo);
            n_div(&term,&cpn,&df);
            n_add(&sum,&sum,&term);
        }
        n_int(&rexpo,N-1); n_add(&rexpo,&rexpo,&t1);
        n_pow(&rdf,&dbase,&rexpo);
        n_div(&rpv,redeem,&rdf);
        n_add(&clean,&sum,&rpv);
        n_sub(&clean,&clean,&accrued);
    }
    n_cpy(out,&clean);
    return dec_check("xprice");
}

struct xbond_args { long settle,maturity; const num *ratepct,*redeem,*price; int freq,basis; };
static int xbond_residual(const num *yldpct, num *out, void *ud)
{
    struct xbond_args *b = ud;
    num pr;
    if (fin_xbond_price(b->settle,b->maturity,b->ratepct,yldpct,b->redeem,b->freq,b->basis,&pr)) return 1;
    n_sub(out,&pr,b->price);
    return 0;
}
int fin_xbond_yield(long settle,long maturity,const num *ratepct,const num *price,
                    const num *redeem,int freq,int basis,num *out)
{
    struct xbond_args b = { settle, maturity, ratepct, redeem, price, freq, basis };
    return solve_rate(xbond_residual, &b, out);
}

int fin_xaccrued(long settle,long maturity,const num *ratepct,int freq,int basis,num *out)
{
    int N; long A,E;
    if (coupon_geometry(settle,maturity,freq,basis,&N,&A,&E)) return 1;
    if (E == 0) { set_err("degenerate coupon period"); return 1; }
    num frq,cpn,An,En,frac;
    n_int(&frq,freq); n_div(&cpn,ratepct,&frq);
    n_int(&An,A); n_int(&En,E); n_div(&frac,&An,&En);
    n_mul(out,&cpn,&frac);
    return dec_check("xaccr");
}
