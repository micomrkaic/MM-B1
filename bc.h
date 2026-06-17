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

/* bc.h -- MM-B1 business RPN calculator: shared declarations.
 *
 * All arithmetic is decimal (IBM decNumber, vendored).  A "value" is a
 * fixed-size decNumber struct, so the stack is a plain array -- no
 * per-number heap management.  Money never touches a binary double.
 */
#ifndef BC_H
#define BC_H

#define DECNUMDIGITS 38            /* must precede decNumber.h; >= ctx.digits */
#include "decNumber.h"
#include "decContext.h"

#include <stdio.h>
#include <string.h>

typedef decNumber num;             /* one stack value */

/* ------- global context + error channel (defined in dec.c) ------------- */
extern decContext g_ctx;
extern char       g_err[256];
extern int        g_dp;            /* display decimal places (default 2)   */

void dec_init(void);
void set_err(const char *fmt, ...);
int  parse_num(const char *tok, num *out);   /* 1 if tok is a finite number */
void fmt_fixed(const num *a, int dp, char *out);   /* fixed-point string    */
void fmt_money(const num *a, int dp, char *out);   /* + thousands separators */

/* hard-error check after an op: returns 1 and sets g_err if the context
 * status shows an invalid operation / div-by-zero / overflow.             */
int  dec_check(const char *where);

/* ------- value helpers (inline, operate through g_ctx) ----------------- */
static inline void n_zero(num *r){ decNumberZero(r); }
static inline void n_str (num *r, const char *s){ decNumberFromString(r,s,&g_ctx); }
static inline void n_int (num *r, long v){ char b[32]; snprintf(b,sizeof b,"%ld",v);
                                           decNumberFromString(r,b,&g_ctx); }
static inline void n_cpy (num *r, const num *a){ decNumberCopy(r,a); }
static inline void n_add (num *r, const num *a, const num *b){ decNumberAdd(r,a,b,&g_ctx); }
static inline void n_sub (num *r, const num *a, const num *b){ decNumberSubtract(r,a,b,&g_ctx); }
static inline void n_mul (num *r, const num *a, const num *b){ decNumberMultiply(r,a,b,&g_ctx); }
static inline void n_div (num *r, const num *a, const num *b){ decNumberDivide(r,a,b,&g_ctx); }
static inline void n_pow (num *r, const num *a, const num *b){ decNumberPower(r,a,b,&g_ctx); }
static inline void n_ln  (num *r, const num *a){ decNumberLn(r,a,&g_ctx); }
static inline void n_exp (num *r, const num *a){ decNumberExp(r,a,&g_ctx); }
static inline void n_sqrt(num *r, const num *a){ decNumberSquareRoot(r,a,&g_ctx); }
static inline void n_neg (num *r, const num *a){ decNumberMinus(r,a,&g_ctx); }
static inline void n_abs (num *r, const num *a){ decNumberAbs(r,a,&g_ctx); }
static inline int  n_iszero(const num *a){ return decNumberIsZero(a); }
static inline int  n_isneg (const num *a){ return decNumberIsNegative(a) && !decNumberIsZero(a); }
static inline int  n_cmp(const num *a, const num *b){      /* -1 / 0 / 1 */
    num t; decNumberCompare(&t,a,b,&g_ctx);
    if (decNumberIsZero(&t)) return 0;
    return decNumberIsNegative(&t) ? -1 : 1;
}

/* ------- stack + registers + cash flows (defined in stack.c) ----------- */
int  st_push(const num *v);
int  st_pop (num *out);
int  st_peek(num *out, int depth);   /* depth 0 == top                     */
int  st_depth(void);
void st_clear(void);
int  st_need(int k);                 /* 1 if at least k items, else err    */

/* TVM registers, all user-facing units (i held in PERCENT per period).    */
extern num T_n, T_i, T_pv, T_pmt, T_fv;
extern int T_begin;                  /* 1 = payments at period start        */
void tvm_reset(void);

/* general storage registers 0..9 */
int  reg_sto(int idx, const num *v);
int  reg_rcl(int idx, num *out);

/* cash-flow bank */
typedef struct { num amt; long cnt; } CF;
extern CF  g_cf[];
extern int g_ncf;
int  cf_add(const num *amt);         /* append flow, count 1                */
int  cf_setcount(long c);            /* set repeat count of last flow       */
void cf_clear(void);
int  cf_total_periods(void);

/* dated cash-flow bank (for XNPV/XIRR); date is YYYYMMDD */
typedef struct { num amt; long date; } XCF;
extern XCF g_xcf[];
extern int g_nxcf;
int  xcf_add(const num *amt, long date);
void xcf_clear(void);

/* ------- finance kernel (defined in finance.c) ------------------------- *
 * Periodic model: i is the per-period rate in PERCENT, n the number of
 * periods.  begin = annuity-due flag.  Sign convention: cash in +, out -. */
int fin_tvm_fv (const num *n,const num *ipct,const num *pv,const num *pmt,int begin,num *out);
int fin_tvm_pv (const num *n,const num *ipct,const num *pmt,const num *fv,int begin,num *out);
int fin_tvm_pmt(const num *n,const num *ipct,const num *pv,const num *fv,int begin,num *out);
int fin_tvm_n  (const num *ipct,const num *pv,const num *pmt,const num *fv,int begin,num *out);
int fin_tvm_i  (const num *n,const num *pv,const num *pmt,const num *fv,int begin,num *out); /* root find -> percent */

int fin_npv(const num *ipct, num *out);              /* over cash-flow bank */
int fin_irr(num *out);                               /* percent            */

int fin_amort(void);                                 /* prints schedule from TVM regs */

int fin_nom2eff(const num *nompct,const num *m,num *out);
int fin_eff2nom(const num *effpct,const num *m,num *out);

int fin_sl  (const num *cost,const num *salv,const num *life,num *out);
int fin_soyd(const num *cost,const num *salv,const num *life,const num *year,num *out);
int fin_db  (const num *cost,const num *salv,const num *life,const num *year,const num *factor,num *out);

int fin_bond_price(const num *cpnpct,const num *yldpct,const num *nper,const num *redeem,num *out);
int fin_bond_yield(const num *price,const num *cpnpct,const num *nper,const num *redeem,num *out); /* percent */

/* ------- calendar (defined in dates.c) -------------------------------- *
 * A date is the integer YYYYMMDD.  Excel day-count basis: 0=30/360 US,
 * 1=actual/actual (ISDA), 2=actual/360, 3=actual/365, 4=30E/360.          */
int  date_to_serial(long ymd, long *serial);
long serial_to_date(long serial);
int  date_days(long ymd1, long ymd2, long *days);   /* actual difference   */
int  date_add(long ymd, long n, long *out);
int  date_dow(long ymd, int *dow);                  /* 0=Sun .. 6=Sat      */
int  year_frac(long ymd1, long ymd2, int basis, num *out);
int  coupon_geometry(long settle,long maturity,int freq,int basis,int *N,long *A,long *E);

/* ------- dated finance (defined in finance.c) ------------------------- *
 * XNPV/XIRR use actual/365 from the first flow's date (Excel convention).
 * Dated bonds use the SIA/Excel price formula with accrued interest.      */
int fin_xnpv(const num *ratepct, num *out);                 /* over dated bank */
int fin_xirr(num *out);                                     /* annual percent  */
int fin_xbond_price(long settle,long maturity,const num *ratepct,const num *yldpct,
                    const num *redeem,int freq,int basis,num *out);        /* clean price */
int fin_xbond_yield(long settle,long maturity,const num *ratepct,const num *price,
                    const num *redeem,int freq,int basis,num *out);        /* annual percent */
int fin_xaccrued(long settle,long maturity,const num *ratepct,int freq,int basis,num *out);

#endif
