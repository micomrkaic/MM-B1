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

/* dates.c -- the calendar layer.
 *
 * A date on the stack is the integer YYYYMMDD (e.g. 20260315).  Everything
 * here is proleptic Gregorian.  Serial days follow Howard Hinnant's
 * branch-free civil<->days algorithm; the epoch is arbitrary because every
 * consumer uses differences only.
 *
 * Day-count "basis" mirrors Excel:
 *   0 = 30/360 (US/NASD)   1 = actual/actual (ISDA)   2 = actual/360
 *   3 = actual/365         4 = 30E/360 (European)
 */
#include "bc.h"

static int is_leap(long y){ return (y%4==0 && y%100!=0) || y%400==0; }
static int dim(long y, int m)
{
    static const int t[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m==2 && is_leap(y)) return 29;
    return t[m-1];
}
static long days_in_year(long y){ return is_leap(y) ? 366 : 365; }

/* civil (y,m,d) -> serial days since 1970-01-01 */
static long civil_to_serial(long y, int m, int d)
{
    y -= m <= 2;
    long era = (y >= 0 ? y : y-399) / 400;
    long yoe = y - era*400;                         /* [0,399] */
    long doy = (153*(m + (m > 2 ? -3 : 9)) + 2)/5 + d - 1;   /* [0,365] */
    long doe = yoe*365 + yoe/4 - yoe/100 + doy;     /* [0,146096] */
    return era*146097 + doe - 719468;
}
static void serial_to_civil(long z, long *yy, int *mm, int *dd)
{
    z += 719468;
    long era = (z >= 0 ? z : z-146096) / 146097;
    long doe = z - era*146097;                      /* [0,146096] */
    long yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;  /* [0,399] */
    long y = yoe + era*400;
    long doy = doe - (365*yoe + yoe/4 - yoe/100);   /* [0,365] */
    long mp = (5*doy + 2)/153;                      /* [0,11] */
    int d = (int)(doy - (153*mp + 2)/5 + 1);        /* [1,31] */
    int m = (int)(mp < 10 ? mp+3 : mp-9);           /* [1,12] */
    *yy = y + (m <= 2);
    *mm = m; *dd = d;
}

int date_to_serial(long ymd, long *serial)
{
    long y = ymd/10000; int m = (int)((ymd/100)%100); int d = (int)(ymd%100);
    if (m < 1 || m > 12 || d < 1 || d > dim(y,m)) { set_err("bad date %ld (use YYYYMMDD)", ymd); return 1; }
    *serial = civil_to_serial(y,m,d);
    return 0;
}
long serial_to_date(long serial)
{
    long y; int m,d; serial_to_civil(serial,&y,&m,&d);
    return y*10000 + m*100 + d;
}
int date_days(long ymd1, long ymd2, long *days)
{
    long s1,s2; if (date_to_serial(ymd1,&s1) || date_to_serial(ymd2,&s2)) return 1;
    *days = s2 - s1; return 0;
}
int date_add(long ymd, long n, long *out)
{
    long s; if (date_to_serial(ymd,&s)) return 1;
    *out = serial_to_date(s + n); return 0;
}
int date_dow(long ymd, int *dow)        /* 0=Sun .. 6=Sat */
{
    long s; if (date_to_serial(ymd,&s)) return 1;
    long w = (s % 7 + 4) % 7; if (w < 0) w += 7;   /* 1970-01-01 was Thursday */
    *dow = (int)w; return 0;
}

/* 30/360 day count.  european=0 -> US/NASD, european=1 -> 30E/360. */
static long days_30_360(long y1,int m1,int d1, long y2,int m2,int d2, int european)
{
    if (european) {
        if (d1==31) d1=30;
        if (d2==31) d2=30;
    } else {
        if (d1==31) d1=30;
        if (d2==31 && d1==30) d2=30;
    }
    return (y2-y1)*360 + (m2-m1)*30 + (d2-d1);
}

/* basis day count between two dates (numerator of the year fraction). */
static int basis_days(long ymd1, long ymd2, int basis, long *out)
{
    long s1,s2; if (date_to_serial(ymd1,&s1) || date_to_serial(ymd2,&s2)) return 1;
    if (basis==0 || basis==4) {
        long y1,y2; int m1,d1,m2,d2;
        serial_to_civil(s1,&y1,&m1,&d1); serial_to_civil(s2,&y2,&m2,&d2);
        *out = days_30_360(y1,m1,d1,y2,m2,d2, basis==4);
    } else {
        *out = s2 - s1;                              /* actual */
    }
    return 0;
}

int year_frac(long ymd1, long ymd2, int basis, num *out)
{
    if (basis < 0 || basis > 4) { set_err("basis 0..4"); return 1; }
    if (basis == 1) {                                /* ISDA actual/actual */
        long s1,s2; if (date_to_serial(ymd1,&s1) || date_to_serial(ymd2,&s2)) return 1;
        long y1,y2; int m1,d1,m2,d2;
        serial_to_civil(s1,&y1,&m1,&d1); serial_to_civil(s2,&y2,&m2,&d2);
        if (y1 == y2) {
            num n,den; n_int(&n, s2-s1); n_int(&den, days_in_year(y1)); n_div(out,&n,&den);
            return dec_check("yearfrac");
        }
        long firstY1n = civil_to_serial(y1+1,1,1);   /* Jan 1 of y1+1 */
        long firstY2  = civil_to_serial(y2,1,1);     /* Jan 1 of y2   */
        num a,b,c,t,d1n,d2n;
        n_int(&t, firstY1n - s1); n_int(&d1n, days_in_year(y1)); n_div(&a,&t,&d1n);
        n_int(&t, s2 - firstY2);  n_int(&d2n, days_in_year(y2)); n_div(&c,&t,&d2n);
        n_int(&b, y2 - y1 - 1);
        n_add(out,&a,&b); n_add(out,out,&c);
        return dec_check("yearfrac");
    }
    long days; if (basis_days(ymd1,ymd2,basis,&days)) return 1;
    long denom = (basis==3) ? 365 : 360;             /* 0,4->360  2->360  3->365 */
    num n,den; n_int(&n,days); n_int(&den,denom); n_div(out,&n,&den);
    return dec_check("yearfrac");
}

/* ---- coupon schedule (for dated bonds) -------------------------------- *
 * Step back from maturity by 12/freq months to bracket settlement.        */
static long step_back_months(long ymd, int months, int eom)
{
    long s; date_to_serial(ymd,&s);
    long y; int m,d; serial_to_civil(s,&y,&m,&d);
    int mm = m - months;
    while (mm < 1) { mm += 12; y -= 1; }
    int last = dim(y,mm);
    d = eom ? last : (d < last ? d : last);
    return civil_to_serial(y,mm,d);
}

/* Returns coupons remaining N (settle<coupon<=maturity), and basis day
 * counts A (PCD->settle) and E (PCD->NCD).  freq in {1,2,3,4,6,12}.        */
int coupon_geometry(long settle_ymd, long maturity_ymd, int freq, int basis,
                    int *N, long *A, long *E)
{
    if (freq <= 0 || 12 % freq != 0) { set_err("frequency must divide 12 (1,2,3,4,6,12)"); return 1; }
    long settle, maturity;
    if (date_to_serial(settle_ymd,&settle) || date_to_serial(maturity_ymd,&maturity)) return 1;
    if (settle >= maturity) { set_err("settlement must precede maturity"); return 1; }

    int p = 12 / freq;
    long my; int mm,md; serial_to_civil(maturity,&my,&mm,&md);
    int eom = (md == dim(my,mm));

    long cur = maturity;                 /* a coupon date (serial)          */
    long ncd = maturity;                 /* smallest coupon > settle        */
    int n = 0;
    while (cur > settle) {
        n++; ncd = cur;
        cur = step_back_months(serial_to_date(cur), p, eom);
    }
    long pcd = cur;                      /* largest coupon <= settle        */

    if (basis_days(serial_to_date(pcd), settle_ymd, basis, A)) return 1;
    if (basis_days(serial_to_date(pcd), serial_to_date(ncd), basis, E)) return 1;
    *N = n;
    return 0;
}
