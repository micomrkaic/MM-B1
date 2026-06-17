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

/* main.c -- MM-B1 business RPN calculator: REPL, tokenizer, word dispatch. */
#define _POSIX_C_SOURCE 200809L     /* strtok_r, getline */
#include "bc.h"
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <readline/readline.h>
#include <readline/history.h>

#define NEED(k) do { if (st_need(k)) return 1; } while (0)

static long to_long(const num *a)
{
    num t, z; n_int(&z,0);
    decNumberRescale(&t, a, &z, &g_ctx);
    long v = decNumberToInt32(&t, &g_ctx);
    decContextZeroStatus(&g_ctx);
    return v;
}
static int pushr(num *r){ if (dec_check("arith")) return 1; return st_push(r); }

/* ---- stack arithmetic ------------------------------------------------- */
static int h_add(void){ NEED(2); num a,b,r; st_pop(&b); st_pop(&a); n_add(&r,&a,&b); return pushr(&r); }
static int h_sub(void){ NEED(2); num a,b,r; st_pop(&b); st_pop(&a); n_sub(&r,&a,&b); return pushr(&r); }
static int h_mul(void){ NEED(2); num a,b,r; st_pop(&b); st_pop(&a); n_mul(&r,&a,&b); return pushr(&r); }
static int h_div(void){ NEED(2); num a,b,r; st_pop(&b); st_pop(&a); n_div(&r,&a,&b); return pushr(&r); }
static int h_pow(void){ NEED(2); num a,b,r; st_pop(&b); st_pop(&a); n_pow(&r,&a,&b); return pushr(&r); }
static int h_chs(void){ NEED(1); num a; st_pop(&a); n_neg(&a,&a); return pushr(&a); }
static int h_inv(void){ NEED(1); num a,one,r; st_pop(&a); n_int(&one,1); n_div(&r,&one,&a); return pushr(&r); }
static int h_sqrt(void){ NEED(1); num a,r; st_pop(&a); n_sqrt(&r,&a); return pushr(&r); }
static int h_ln(void){ NEED(1); num a,r; st_pop(&a); n_ln(&r,&a); return pushr(&r); }
static int h_exp(void){ NEED(1); num a,r; st_pop(&a); n_exp(&r,&a); return pushr(&r); }
static int h_abs(void){ NEED(1); num a,r; st_pop(&a); n_abs(&r,&a); return pushr(&r); }

static int h_pct(void){   NEED(2); num y,x,r,h; st_pop(&x); st_pop(&y); n_int(&h,100);
                          n_mul(&r,&y,&x); n_div(&r,&r,&h); return pushr(&r); }
static int h_pctchg(void){NEED(2); num y,x,d,r,h; st_pop(&x); st_pop(&y); n_int(&h,100);
                          n_sub(&d,&x,&y); n_div(&r,&d,&y); n_mul(&r,&r,&h); return pushr(&r); }
static int h_markup(void){NEED(2); num c,p,d,r,h; st_pop(&p); st_pop(&c); n_int(&h,100);
                          n_sub(&d,&p,&c); n_div(&r,&d,&c); n_mul(&r,&r,&h); return pushr(&r); }
static int h_margin(void){NEED(2); num c,p,d,r,h; st_pop(&p); st_pop(&c); n_int(&h,100);
                          n_sub(&d,&p,&c); n_div(&r,&d,&p); n_mul(&r,&r,&h); return pushr(&r); }

/* ---- stack manipulation ---------------------------------------------- */
static int h_dup(void){ NEED(1); num a; st_peek(&a,0); return st_push(&a); }
static int h_drop(void){ NEED(1); num a; return st_pop(&a); }
static int h_swap(void){ NEED(2); num a,b; st_pop(&b); st_pop(&a); st_push(&b); return st_push(&a); }
static int h_over(void){ NEED(2); num a; st_peek(&a,1); return st_push(&a); }
static int h_depth(void){ num d; n_int(&d,st_depth()); return st_push(&d); }
static int h_clst(void){ st_clear(); return 0; }
static int h_pi(void){ num a; n_str(&a,"3.1415926535897932384626433832795029"); return st_push(&a); }
static int h_e(void){  num a; n_str(&a,"2.7182818284590452353602874713526625"); return st_push(&a); }

static int h_dot(void){ NEED(1); num a; st_pop(&a); char b[160]; fmt_fixed(&a,g_dp,b); printf("%s\n",b); return 0; }
static int h_dots(void){
    int d = st_depth();
    if (d == 0) { printf("(empty)\n"); return 0; }
    for (int i = d-1; i >= 0; i--) { num a; st_peek(&a,i); char b[160]; fmt_fixed(&a,g_dp,b);
        printf("  [%d] %s\n", i, b); }
    return 0;
}

/* ---- TVM -------------------------------------------------------------- */
static int set_reg(num *R){ NEED(1); num a; st_pop(&a); n_cpy(R,&a); return 0; }
static int h_set_n(void){ return set_reg(&T_n); }
static int h_set_i(void){ return set_reg(&T_i); }
static int h_set_pv(void){ return set_reg(&T_pv); }
static int h_set_pmt(void){ return set_reg(&T_pmt); }
static int h_set_fv(void){ return set_reg(&T_fv); }
static int h_begin(void){ T_begin = 1; return 0; }
static int h_end(void){ T_begin = 0; return 0; }
static int h_clrtvm(void){ tvm_reset(); return 0; }

static int h_solve_pv(void){ num r; if (fin_tvm_pv(&T_n,&T_i,&T_pmt,&T_fv,T_begin,&r)) return 1;
                             n_cpy(&T_pv,&r); return st_push(&r); }
static int h_solve_fv(void){ num r; if (fin_tvm_fv(&T_n,&T_i,&T_pv,&T_pmt,T_begin,&r)) return 1;
                             n_cpy(&T_fv,&r); return st_push(&r); }
static int h_solve_pmt(void){ num r; if (fin_tvm_pmt(&T_n,&T_i,&T_pv,&T_fv,T_begin,&r)) return 1;
                              n_cpy(&T_pmt,&r); return st_push(&r); }
static int h_solve_n(void){ num r; if (fin_tvm_n(&T_i,&T_pv,&T_pmt,&T_fv,T_begin,&r)) return 1;
                            n_cpy(&T_n,&r); return st_push(&r); }
static int h_solve_i(void){ num r; if (fin_tvm_i(&T_n,&T_pv,&T_pmt,&T_fv,T_begin,&r)) return 1;
                            n_cpy(&T_i,&r); return st_push(&r); }

static int h_tvm(void){
    char b[160];
    fmt_fixed(&T_n,g_dp,b);   printf("  n   = %s\n", b);
    fmt_fixed(&T_i,g_dp,b);   printf("  i   = %s %% per period\n", b);
    fmt_fixed(&T_pv,g_dp,b);  printf("  pv  = %s\n", b);
    fmt_fixed(&T_pmt,g_dp,b); printf("  pmt = %s\n", b);
    fmt_fixed(&T_fv,g_dp,b);  printf("  fv  = %s\n", b);
    printf("  mode = %s\n", T_begin ? "begin" : "end");
    return 0;
}

/* ---- cash flows ------------------------------------------------------- */
static int h_cf(void){ NEED(1); num a; st_pop(&a); return cf_add(&a); }
static int h_cfn(void){ NEED(1); num a; st_pop(&a); return cf_setcount(to_long(&a)); }
static int h_clrcf(void){ cf_clear(); return 0; }
static int h_cfs(void){
    if (g_ncf == 0) { printf("(no cash flows)\n"); return 0; }
    char b[160]; int t = 0;
    for (int j = 0; j < g_ncf; j++) { fmt_fixed(&g_cf[j].amt,g_dp,b);
        printf("  cf[%d] t=%d..%ld  %s  (x%ld)\n", j, t, t+g_cf[j].cnt-1, b, g_cf[j].cnt);
        t += (int)g_cf[j].cnt; }
    return 0;
}
static int h_npv(void){ NEED(1); num rate,r; st_pop(&rate); if (fin_npv(&rate,&r)) return 1; return st_push(&r); }
static int h_irr(void){ num r; if (fin_irr(&r)) return 1; return st_push(&r); }

/* ---- amortization, rates, depreciation, bonds ------------------------- */
static int h_amort(void){ return fin_amort(); }
static int h_nom2eff(void){ NEED(2); num nom,m,r; st_pop(&m); st_pop(&nom); if (fin_nom2eff(&nom,&m,&r)) return 1; return st_push(&r); }
static int h_eff2nom(void){ NEED(2); num eff,m,r; st_pop(&m); st_pop(&eff); if (fin_eff2nom(&eff,&m,&r)) return 1; return st_push(&r); }

static int h_sl(void){   NEED(3); num c,s,l,r; st_pop(&l); st_pop(&s); st_pop(&c);
                         if (fin_sl(&c,&s,&l,&r)) return 1;
                         return st_push(&r); }
static int h_soyd(void){ NEED(4); num c,s,l,y,r; st_pop(&y); st_pop(&l); st_pop(&s); st_pop(&c);
                         if (fin_soyd(&c,&s,&l,&y,&r)) return 1;
                         return st_push(&r); }
static int h_db(void){   NEED(5); num c,s,l,y,f,r; st_pop(&f); st_pop(&y); st_pop(&l); st_pop(&s); st_pop(&c);
                         if (fin_db(&c,&s,&l,&y,&f,&r)) return 1;
                         return st_push(&r); }

static int h_bprice(void){ NEED(4); num cpn,yld,n,red,r; st_pop(&red); st_pop(&n); st_pop(&yld); st_pop(&cpn);
                           if (fin_bond_price(&cpn,&yld,&n,&red,&r)) return 1;
                           return st_push(&r); }
static int h_byield(void){ NEED(4); num price,cpn,n,red,r; st_pop(&red); st_pop(&n); st_pop(&cpn); st_pop(&price);
                           if (fin_bond_yield(&price,&cpn,&n,&red,&r)) return 1;
                           return st_push(&r); }

/* ---- dates ------------------------------------------------------------ */
static int h_ddays(void){ NEED(2); num d1,d2; st_pop(&d2); st_pop(&d1);
    long days; if (date_days(to_long(&d1),to_long(&d2),&days)) return 1;
    num r; n_int(&r,days); return st_push(&r); }
static int h_dateadd(void){ NEED(2); num d,nd; st_pop(&nd); st_pop(&d);
    long out; if (date_add(to_long(&d),to_long(&nd),&out)) return 1;
    num r; n_int(&r,out); return st_push(&r); }
static int h_dow(void){ NEED(1); num d; st_pop(&d);
    int w; if (date_dow(to_long(&d),&w)) return 1;
    num r; n_int(&r,w); return st_push(&r); }
static int h_yearfrac(void){ NEED(3); num d1,d2,b,r; st_pop(&b); st_pop(&d2); st_pop(&d1);
    if (year_frac(to_long(&d1),to_long(&d2),(int)to_long(&b),&r)) return 1;
    return st_push(&r); }

/* ---- dated cash flows ------------------------------------------------- */
static int h_xcf(void){ NEED(2); num amt,d; st_pop(&d); st_pop(&amt); return xcf_add(&amt,to_long(&d)); }
static int h_clrxcf(void){ xcf_clear(); return 0; }
static int h_xcfs(void){
    if (g_nxcf == 0) { printf("(no dated cash flows)\n"); return 0; }
    char b[160];
    for (int j = 0; j < g_nxcf; j++) { fmt_fixed(&g_xcf[j].amt,g_dp,b);
        printf("  xcf[%d]  %ld  %s\n", j, g_xcf[j].date, b); }
    return 0;
}
static int h_xnpv(void){ NEED(1); num rate,r; st_pop(&rate); if (fin_xnpv(&rate,&r)) return 1; return st_push(&r); }
static int h_xirr(void){ num r; if (fin_xirr(&r)) return 1; return st_push(&r); }

/* ---- dated bonds ------------------------------------------------------ */
static int h_xprice(void){ NEED(7);
    num settle,mat,rate,yld,redeem,freq,basis,r;
    st_pop(&basis); st_pop(&freq); st_pop(&redeem); st_pop(&yld); st_pop(&rate); st_pop(&mat); st_pop(&settle);
    if (fin_xbond_price(to_long(&settle),to_long(&mat),&rate,&yld,&redeem,(int)to_long(&freq),(int)to_long(&basis),&r)) return 1;
    return st_push(&r); }
static int h_xyield(void){ NEED(7);
    num settle,mat,rate,price,redeem,freq,basis,r;
    st_pop(&basis); st_pop(&freq); st_pop(&redeem); st_pop(&price); st_pop(&rate); st_pop(&mat); st_pop(&settle);
    if (fin_xbond_yield(to_long(&settle),to_long(&mat),&rate,&price,&redeem,(int)to_long(&freq),(int)to_long(&basis),&r)) return 1;
    return st_push(&r); }
static int h_xaccr(void){ NEED(5);
    num settle,mat,rate,freq,basis,r;
    st_pop(&basis); st_pop(&freq); st_pop(&rate); st_pop(&mat); st_pop(&settle);
    if (fin_xaccrued(to_long(&settle),to_long(&mat),&rate,(int)to_long(&freq),(int)to_long(&basis),&r)) return 1;
    return st_push(&r); }

/* ---- format / meta ---------------------------------------------------- */
static int h_fix(void){ NEED(1); num a; st_pop(&a); long d = to_long(&a);
                        if (d < 0 || d > 16) { set_err("fix 0..16"); return 1; } g_dp = (int)d; return 0; }

static int h_help(void);
static int h_words(void);
static int g_quit = 0;
static int h_quit(void){ g_quit = 1; return 0; }

struct word { const char *name; int (*fn)(void); const char *desc; };
static const struct word words[] = {
  {"+",h_add,"a b -> a+b"}, {"-",h_sub,"a b -> a-b"}, {"*",h_mul,"a b -> a*b"},
  {"/",h_div,"a b -> a/b"}, {"^",h_pow,"a b -> a^b"}, {"chs",h_chs,"negate top"},
  {"inv",h_inv,"1/x"}, {"sqrt",h_sqrt,"square root"}, {"ln",h_ln,"natural log"},
  {"exp",h_exp,"e^x"}, {"abs",h_abs,"absolute value"},
  {"pct",h_pct,"y x -> x% of y"}, {"%chg",h_pctchg,"y x -> percent change"},
  {"markup",h_markup,"cost price -> markup %"}, {"margin",h_margin,"cost price -> margin %"},
  {"dup",h_dup,"duplicate top"}, {"drop",h_drop,"discard top"}, {"swap",h_swap,"swap top two"},
  {"over",h_over,"copy 2nd to top"}, {"depth",h_depth,"push stack depth"}, {"clst",h_clst,"clear stack"},
  {"pi",h_pi,"push pi"}, {"e",h_e,"push e"},
  {".",h_dot,"pop and print top"}, {".s",h_dots,"show stack"},
  {"n",h_set_n,"store top -> N"}, {"i",h_set_i,"store top -> I (% per period)"},
  {"pv",h_set_pv,"store top -> PV"}, {"pmt",h_set_pmt,"store top -> PMT"}, {"fv",h_set_fv,"store top -> FV"},
  {"?n",h_solve_n,"solve for N"}, {"?i",h_solve_i,"solve for I (%)"}, {"?pv",h_solve_pv,"solve for PV"},
  {"?pmt",h_solve_pmt,"solve for PMT"}, {"?fv",h_solve_fv,"solve for FV"},
  {"begin",h_begin,"payments at period start"}, {"end",h_end,"payments at period end"},
  {"clrtvm",h_clrtvm,"reset TVM registers"}, {"tvm",h_tvm,"show TVM registers"},
  {"cf",h_cf,"append cash flow"}, {"cfn",h_cfn,"set repeat count of last flow"},
  {"clrcf",h_clrcf,"clear cash flows"}, {"cfs",h_cfs,"list cash flows"},
  {"npv",h_npv,"rate% -> NPV of cash flows"}, {"irr",h_irr,"IRR % of cash flows"},
  {"amort",h_amort,"amortization schedule from TVM regs"},
  {"nom>eff",h_nom2eff,"nom% m -> effective annual %"}, {"eff>nom",h_eff2nom,"eff% m -> nominal annual %"},
  {"sl",h_sl,"cost salv life -> straight-line dep"},
  {"soyd",h_soyd,"cost salv life year -> SOYD dep"},
  {"db",h_db,"cost salv life year factor -> declining-balance dep"},
  {"bprice",h_bprice,"cpn% yld% n redeem -> bond price (periodic)"},
  {"byield",h_byield,"price cpn% n redeem -> bond yield % (periodic)"},
  {"ddays",h_ddays,"d1 d2 -> actual days between (YYYYMMDD)"},
  {"date+",h_dateadd,"date ndays -> date shifted by days"},
  {"dow",h_dow,"date -> day of week (0=Sun..6=Sat)"},
  {"yearfrac",h_yearfrac,"d1 d2 basis -> year fraction (basis 0..4)"},
  {"xcf",h_xcf,"amount date -> append dated cash flow"},
  {"clrxcf",h_clrxcf,"clear dated cash flows"}, {"xcfs",h_xcfs,"list dated cash flows"},
  {"xnpv",h_xnpv,"rate% -> XNPV of dated flows (act/365)"},
  {"xirr",h_xirr,"XIRR % of dated flows"},
  {"xprice",h_xprice,"settle mat cpn% yld% redeem freq basis -> clean price"},
  {"xyield",h_xyield,"settle mat cpn% price redeem freq basis -> yield %"},
  {"xaccr",h_xaccr,"settle mat cpn% freq basis -> accrued interest"},
  {"fix",h_fix,"set display decimals"},
  {"help",h_help,"this help"}, {"words",h_words,"list all words"},
  {"quit",h_quit,"exit"}, {"exit",h_quit,"exit"},
};
static const int NWORDS = (int)(sizeof words / sizeof words[0]);

static int h_words(void){
    for (int i = 0; i < NWORDS; i++) printf("  %-9s %s\n", words[i].name, words[i].desc);
    return 0;
}
static int h_help(void){
    printf(
"MM-B1 -- decimal business RPN calculator.\n"
"  RPN: operands first, then the word.  e.g.  3 4 +   ->   7\n"
"  All money math is exact decimal (IBM decNumber), half-even rounding.\n"
"\n"
"  TVM (periodic model): set any four of N I PV PMT FV, solve the fifth.\n"
"    I is the rate per PERIOD in percent; N is the number of periods.\n"
"    Sign convention: cash received +, cash paid -.\n"
"    12  n   1  i   10000  pv   0  fv   ?pmt\n"
"\n"
"  Cash flows:  -1000 cf  300 cf  4 cfn   15 npv   irr\n"
"  Rates:       12 12 nom>eff      Depreciation: 10000 1000 5 sl\n"
"  Bonds:       3 3.5 20 100 bprice (periodic coupon/yield, n, redemption)\n"
"\n"
"  Dates are YYYYMMDD integers (20260315).  Day-count basis 0..4 as in Excel.\n"
"    20070115 20070315 ddays      actual days        date ndays date+\n"
"    dated cash flows: amount date xcf ... then  rate%% xnpv  /  xirr\n"
"    dated bonds: settle mat cpn%% yld%% redeem freq basis xprice (and xyield)\n"
"\n"
"  'words' lists everything; 'tvm' shows registers; 'fix N' sets decimals.\n");
    return 0;
}

/* ---- dispatch + REPL -------------------------------------------------- */
static void lower(char *s){ for (; *s; s++) *s = (char)tolower((unsigned char)*s); }

/* returns 0 ok, 1 error (g_err set), 2 quit */
static int execute(const char *tok)
{
    num v;
    if (parse_num(tok, &v)) { return st_push(&v) ? 1 : 0; }

    char lc[64];
    snprintf(lc, sizeof lc, "%s", tok);
    lower(lc);

    /* storage registers: s0..s9 (store, leaves stack), r0..r9 (recall) */
    if ((lc[0]=='s'||lc[0]=='r') && lc[1]>='0' && lc[1]<='9' && lc[2]=='\0') {
        int idx = lc[1]-'0';
        if (lc[0]=='s') { num a; if (st_need(1)) return 1; st_peek(&a,0); return reg_sto(idx,&a)?1:0; }
        else            { num a; if (reg_rcl(idx,&a)) return 1; return st_push(&a)?1:0; }
    }
    for (int i = 0; i < NWORDS; i++)
        if (strcmp(lc, words[i].name) == 0) {
            int rc = words[i].fn();
            if (g_quit) return 2;
            return rc;
        }
    set_err("unknown word: %s", tok);
    return 1;
}

/* process one line; stops at first error on the line */
static int run_line(char *line)
{
    char *hash = strchr(line, '#'); if (hash) *hash = '\0';   /* comment */
    char *save = NULL;
    for (char *t = strtok_r(line, " \t\r\n", &save); t; t = strtok_r(NULL, " \t\r\n", &save)) {
        int rc = execute(t);
        if (rc == 2) return 2;
        if (rc == 1) { printf("error: %s\n", g_err); g_err[0]='\0'; return 1; }
    }
    return 0;
}

static void splash(void)
{
    fputs(
        "\n"
        "   \xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x95\x97   \xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x95\x97\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x95\x97   \xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x95\x97      \xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x95\x97  \xE2\x96\x88\xE2\x96\x88\xE2\x95\x97\n"
        "   \xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x95\x97 \xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x95\x91\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x95\x97 \xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x95\x91      \xE2\x96\x88\xE2\x96\x88\xE2\x95\x94\xE2\x95\x90\xE2\x95\x90\xE2\x96\x88\xE2\x96\x88\xE2\x95\x97\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x95\x91\n"
        "   \xE2\x96\x88\xE2\x96\x88\xE2\x95\x94\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x95\x94\xE2\x96\x88\xE2\x96\x88\xE2\x95\x91\xE2\x96\x88\xE2\x96\x88\xE2\x95\x94\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x95\x94\xE2\x96\x88\xE2\x96\x88\xE2\x95\x91\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x95\x97\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x95\x94\xE2\x95\x9D\xE2\x95\x9A\xE2\x96\x88\xE2\x96\x88\xE2\x95\x91\n"
        "   \xE2\x96\x88\xE2\x96\x88\xE2\x95\x91\xE2\x95\x9A\xE2\x96\x88\xE2\x96\x88\xE2\x95\x94\xE2\x95\x9D\xE2\x96\x88\xE2\x96\x88\xE2\x95\x91\xE2\x96\x88\xE2\x96\x88\xE2\x95\x91\xE2\x95\x9A\xE2\x96\x88\xE2\x96\x88\xE2\x95\x94\xE2\x95\x9D\xE2\x96\x88\xE2\x96\x88\xE2\x95\x91\xE2\x95\x9A\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x9D\xE2\x96\x88\xE2\x96\x88\xE2\x95\x94\xE2\x95\x90\xE2\x95\x90\xE2\x96\x88\xE2\x96\x88\xE2\x95\x97 \xE2\x96\x88\xE2\x96\x88\xE2\x95\x91\n"
        "   \xE2\x96\x88\xE2\x96\x88\xE2\x95\x91 \xE2\x95\x9A\xE2\x95\x90\xE2\x95\x9D \xE2\x96\x88\xE2\x96\x88\xE2\x95\x91\xE2\x96\x88\xE2\x96\x88\xE2\x95\x91 \xE2\x95\x9A\xE2\x95\x90\xE2\x95\x9D \xE2\x96\x88\xE2\x96\x88\xE2\x95\x91      \xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x95\x94\xE2\x95\x9D \xE2\x96\x88\xE2\x96\x88\xE2\x95\x91\n"
        "   \xE2\x95\x9A\xE2\x95\x90\xE2\x95\x9D     \xE2\x95\x9A\xE2\x95\x90\xE2\x95\x9D\xE2\x95\x9A\xE2\x95\x90\xE2\x95\x9D     \xE2\x95\x9A\xE2\x95\x90\xE2\x95\x9D      \xE2\x95\x9A\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x9D  \xE2\x95\x9A\xE2\x95\x90\xE2\x95\x9D\n"
        "\n"
        "     decimal business RPN calculator   \xC2\xB7   v1.1\n"
        "     \xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\n"
        "     exact base-10 money  \xC2\xB7  TVM  \xC2\xB7  cash flows  \xC2\xB7  bonds  \xC2\xB7  dates\n"
        "     the live stack shows newest at the bottom (x)\n"
        "     type  help  or  words      \xC2\xB7      ^D to quit\n"
        "\n", stdout);
}

/* live calculator display: whole stack, newest (x) at the bottom */
static void show_stack(void)
{
    int d = st_depth();
    if (d == 0) { printf("    \xC2\xB7 empty\n"); return; }
    const char *names = "xyzt";
    for (int i = d - 1; i >= 0; i--) {
        num a; st_peek(&a, i);
        char val[160]; fmt_money(&a, g_dp, val);
        char lbl[16];
        if (i < 4) snprintf(lbl, sizeof lbl, "%c", names[i]);
        else       snprintf(lbl, sizeof lbl, "%d", i + 1);
        printf("  %2s \xE2\x94\x82 %18s\n", lbl, val);
    }
}

int main(void)
{
    dec_init();
    tvm_reset();

    int interactive = isatty(STDIN_FILENO);
    if (interactive) {
        splash();
        for (;;) {
            char *line = readline("mmb> ");
            if (!line) { printf("\n"); break; }
            if (*line) add_history(line);
            int rc = run_line(line);
            free(line);
            if (rc == 2) break;
            show_stack();
        }
    } else {
        char *line = NULL; size_t cap = 0; ssize_t len;
        while ((len = getline(&line, &cap, stdin)) != -1)
            if (run_line(line) == 2) break;
        free(line);
    }
    return 0;
}
