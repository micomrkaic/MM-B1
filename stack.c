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

/* stack.c -- decimal stack, TVM registers, storage registers, cash flows. */
#include "bc.h"

#define STK_CAP 4096
#define CF_CAP  1024

static num stk[STK_CAP];
static int sp = 0;

num T_n, T_i, T_pv, T_pmt, T_fv;
int T_begin = 0;

static num reg[10];

CF  g_cf[CF_CAP];
int g_ncf = 0;

XCF g_xcf[CF_CAP];
int g_nxcf = 0;

/* ----- stack ----- */
int st_push(const num *v)
{
    if (sp >= STK_CAP) { set_err("stack overflow"); return 1; }
    n_cpy(&stk[sp++], v);
    return 0;
}
int st_pop(num *out)
{
    if (sp <= 0) { set_err("stack empty"); return 1; }
    n_cpy(out, &stk[--sp]);
    return 0;
}
int st_peek(num *out, int depth)
{
    if (depth < 0 || depth >= sp) { set_err("stack too shallow"); return 1; }
    n_cpy(out, &stk[sp - 1 - depth]);
    return 0;
}
int st_depth(void){ return sp; }
void st_clear(void){ sp = 0; }
int st_need(int k)
{
    if (sp < k) { set_err("need %d value%s, have %d", k, k==1?"":"s", sp); return 1; }
    return 0;
}

/* ----- TVM ----- */
void tvm_reset(void)
{
    n_zero(&T_n); n_zero(&T_i); n_zero(&T_pv); n_zero(&T_pmt); n_zero(&T_fv);
    T_begin = 0;
}

/* ----- storage regs ----- */
int reg_sto(int idx, const num *v)
{
    if (idx < 0 || idx > 9) { set_err("register 0..9 only"); return 1; }
    n_cpy(&reg[idx], v); return 0;
}
int reg_rcl(int idx, num *out)
{
    if (idx < 0 || idx > 9) { set_err("register 0..9 only"); return 1; }
    n_cpy(out, &reg[idx]); return 0;
}

/* ----- cash flows ----- */
int cf_add(const num *amt)
{
    if (g_ncf >= CF_CAP) { set_err("cash-flow bank full"); return 1; }
    n_cpy(&g_cf[g_ncf].amt, amt);
    g_cf[g_ncf].cnt = 1;
    g_ncf++;
    return 0;
}
int cf_setcount(long c)
{
    if (g_ncf == 0) { set_err("no cash flow to repeat"); return 1; }
    if (c < 1)      { set_err("count must be >= 1");     return 1; }
    g_cf[g_ncf - 1].cnt = c;
    return 0;
}
void cf_clear(void){ g_ncf = 0; }
int cf_total_periods(void)
{
    int t = 0;
    for (int k = 0; k < g_ncf; k++) t += (int)g_cf[k].cnt;
    return t;   /* includes the t=0 flow as one entry */
}

/* ----- dated cash flows ----- */
int xcf_add(const num *amt, long date)
{
    if (g_nxcf >= CF_CAP) { set_err("dated cash-flow bank full"); return 1; }
    n_cpy(&g_xcf[g_nxcf].amt, amt);
    g_xcf[g_nxcf].date = date;
    g_nxcf++;
    return 0;
}
void xcf_clear(void){ g_nxcf = 0; }
