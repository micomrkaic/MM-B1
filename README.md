# MM-B1 — a decimal business RPN calculator

A stack (RPN) calculator for financial work, in the HP-12C lineage but with an
economist's conventions instead of the 12C's. Companion to
[MM-11](https://github.com/micomrkaic/MM-11) and
[MM-14](https://github.com/micomrkaic/MM-14): same shape — tokenizer, stack,
registers, a word-dispatch REPL on GNU readline — but the arithmetic is
**decimal**, not binary, and there is a real calendar underneath the bond and
cash-flow math.

```
   ███╗   ███╗███╗   ███╗      ██████╗  ██╗
   ████╗ ████║████╗ ████║      ██╔══██╗███║
   ██╔████╔██║██╔████╔██║█████╗██████╔╝╚██║
   ██║╚██╔╝██║██║╚██╔╝██║╚════╝██╔══██╗ ██║
   ██║ ╚═╝ ██║██║ ╚═╝ ██║      ██████╔╝ ██║
   ╚═╝     ╚═╝╚═╝     ╚═╝      ╚═════╝  ╚═╝
```

## Why decimal

Money is decimal. Binary floating point cannot represent `0.10`, so a long run
of binary arithmetic drifts away from the ledger in the last place and you find
out during reconciliation. MM-B1 does every operation in base 10 with explicit
round-half-even (banker's) rounding, so results agree with SQL `NUMERIC`, COBOL
packed decimal, Java `BigDecimal`, and Python's `decimal` — all the same
specification. The engine is **IBM decNumber**, the C reference implementation
of the General Decimal Arithmetic Specification, vendored under
`vendor/decNumber/` so there is no external decimal dependency. Working
precision is 34 digits; the display rounds to a configurable number of places.

```
mmb> 0.1 0.2 +
   x │               0.30
```

## Build

The only external dependency is GNU readline.

```sh
make            # builds ./mmb1
make test       # runs the regression script tests.mmb
make run        # builds and starts an interactive session
```

**Linux:** `sudo apt install libreadline-dev`, then `make`.
**macOS:** `brew install readline && make READLINE_PREFIX="$(brew --prefix readline)"`.

## The live stack

Type numbers and words; the whole stack is printed after every line, newest at
the bottom. The bottom four levels are named `x y z t` (HP convention, `x` is
the live value); deeper levels are numbered. You do **not** type `.` to see
results — that is for scripts.

```
mmb> 3 4 +
   x │               7.00
mmb> 10 *
   x │              70.00
mmb> 2 fix
mmb> 1234567.5 dup +
   y │              70.00
   x │       2,469,135.00
```

`clst` clears the stack; `dup drop swap over` rearrange it; `N fix` sets the
number of displayed decimals (the stored value keeps full precision).

## RPN and the TVM model

Operands first, then the word — postfix everywhere, including settings
(`4 fix`, not `fix 4`).

**Time value of money** uses a *periodic* model: `i` is the rate **per period
in percent** and `n` is the **number of periods**. A 6% annual loan paid
monthly is `0.5 i` with `n` in months — no hidden annualizing. Set any four of
`n i pv pmt fv`, then solve the fifth with `?n ?i ?pv ?pmt ?fv`. Cash received
is positive, cash paid is negative; `begin`/`end` switch annuity-due vs
ordinary; `tvm` shows the registers, `clrtvm` resets them.

---

## Worked examples

### 1. A mortgage

A $300,000 loan, 30 years, 6% annual interest, paid monthly. Rate per month is
`0.5`, term is `360` months.

```
clrtvm
360 n  0.5 i  300000 pv  0 fv  end
?pmt          ->  -1,798.65     monthly payment (negative = you pay it)
```

Now see where the money goes, and the running balance:

```
amort         ->  full schedule: interest, principal, balance, per month
```

### 2. Saving toward a goal

Put away $500 at the end of every month for 30 years, earning 6% annual.

```
clrtvm
360 n  0.5 i  0 pv  -500 pmt
?fv           ->  502,257.52    balance after 30 years
```

How much must you deposit **at the start** of each month to reach $1,000,000
in 25 years at 7% annual? Annuity due, so `begin`:

```
clrtvm
300 n  0.5833333333 i  0 pv  1000000 fv  begin
?pmt          ->  -1,227.30     required monthly deposit
```

### 3. The true rate on a loan

You borrow $10,000 and repay $500 a month for 24 months. What rate are you
actually paying?

```
clrtvm
24 n  10000 pv  -500 pmt  0 fv
?i            ->  1.5131        percent per month  (~18.16% APR)
```

### 4. Project appraisal — NPV and IRR

A project costs $50,000 today and returns $15,000 a year for five years.

```
clrcf
-50000 cf  15000 cf  5 cfn
10 npv        ->  6,861.80      net present value at 10%
irr           ->  15.2382       internal rate of return, % per year
```

`cf` appends a flow (the first is period 0, undiscounted); `cfn` sets a repeat
count on the most recent flow; `cfs` lists the bank; `clrcf` empties it.

### 5. Uneven, real-dated cash flows — XNPV and XIRR

When flows land on actual calendar dates, discount by actual/365 from the first
date (the Excel XNPV/XIRR convention). Dates are plain `YYYYMMDD` integers.

```
clrxcf
-10000 20080101 xcf
2750 20080301 xcf   4250 20081030 xcf   3250 20090215 xcf   2750 20090401 xcf
9 xnpv        ->  2,086.65      value at 9% per year
xirr          ->  37.3363       money-weighted annual return, %
```

### 6. Stated vs. real interest rates

A credit card quotes 18% APR compounded monthly. The rate you actually pay over
a year is higher:

```
18 12 nom>eff ->  19.5618       effective annual rate, %
```

`eff>nom` goes the other way (effective to nominal, given compounding periods).

### 7. Depreciation schedules

A $50,000 machine, $5,000 salvage, 7-year life. Three methods, first year:

```
50000 5000 7 sl          ->   6,428.57    straight line (same every year)
50000 5000 7 1 soyd      ->  11,250.00    sum-of-years-digits, year 1
50000 5000 7 1 2 db      ->  14,285.71    double-declining (factor 2), year 1
```

### 8. Bonds without a calendar

Quick pricing on a coupon date, periodic coupon and yield: a 3% periodic coupon
priced to a 3.5% periodic yield over 20 periods, redeemed at 100.

```
3 3.5 20 100 bprice      ->  92.89        price (a discount bond, coupon < yield)
92.89 3 20 100 byield    ->   3.5003      yield recovered from that price
```

### 9. Bonds with a calendar — accrued interest and settlement dates

`xprice`/`xyield` take settlement and maturity dates, annual coupon and yield,
redemption per 100, coupon frequency, and day-count basis (Excel numbering:
0 = 30/360 US, 1 = act/act, 2 = act/360, 3 = act/365, 4 = 30E/360). Coupon dates
are generated back from maturity with end-of-month handling, and the clean price
nets out accrued interest.

A note settling 15 Jun 2026, maturing 15 May 2036, 4.25% coupon, priced to a
4.5% yield, semiannual, actual/actual:

```
# settle    maturity  cpn% yld% redeem freq basis
20260615 20360515 4.25 4.5 100 2 1 xprice   ->  98.0146   clean price
20260615 20360515 4.25 2 1        xaccr     ->   0.3580   accrued per 100
```

The dirty (invoice) price is the clean price plus accrued. `xyield` inverts
`xprice`: give it a price instead of a yield and it returns the yield.

### 10. Date arithmetic

```
20070115 20070315 ddays        ->  59           actual days between
20260315 30 date+              ->  20260414     shift a date by days
20260315 dow                   ->  0            day of week (0=Sun..6=Sat)
20070101 20080101 3 yearfrac   ->  1.0000       year fraction, act/365
```

---

## Word reference

| group | words |
|-------|-------|
| arithmetic | `+ - * / ^ chs inv sqrt ln exp abs` |
| business % | `pct` (x% of y), `%chg`, `markup`, `margin` |
| stack | `dup drop swap over depth clst . .s` |
| constants | `pi e` |
| TVM set | `n i pv pmt fv` |
| TVM solve | `?n ?i ?pv ?pmt ?fv` |
| TVM mode | `begin end clrtvm tvm` |
| cash flows | `cf cfn clrcf cfs npv irr` |
| amortization | `amort` |
| rate conversion | `nom>eff` `eff>nom` |
| depreciation | `sl soyd db` |
| bonds (periodic) | `bprice byield` |
| dates | `ddays date+ dow yearfrac` |
| dated cash flows | `xcf clrxcf xcfs xnpv xirr` |
| dated bonds | `xprice xyield xaccr` |
| storage | `s0..s9` (store, leaves stack) `r0..r9` (recall) |
| display/meta | `fix help words quit` |

`help` and `words` print this at the prompt. Lines may carry `#` comments, and
the program reads a piped script the same way it reads the keyboard, so
`./mmb1 < script.mmb` works for batch runs and regression tests (in batch mode
the stack is not auto-printed; use `.` / `.s`).

### Argument order (everything postfix)

```
y x pct                      x% of y
cost price markup            markup % on cost
cost price margin            margin % on price
cost salv life sl            straight-line depreciation per year
cost salv life year soyd     sum-of-years-digits for that year
cost salv life year factor db   declining balance for that year
nom% m nom>eff               effective annual from nominal, m periods/yr
cpn% yld% n redeem bprice    periodic bond price
price cpn% n redeem byield   periodic bond yield
d1 d2 ddays                  actual days between two YYYYMMDD dates
date ndays date+             shift a date by a number of days
d1 d2 basis yearfrac         year fraction, Excel basis 0..4
amount date xcf              append a dated cash flow
rate% xnpv                   XNPV of dated flows (act/365 from first date)
settle mat cpn% yld% redeem freq basis xprice   dated bond clean price
settle mat cpn% price redeem freq basis xyield  dated bond yield %
settle mat cpn% freq basis xaccr                accrued interest per 100
```

## Numerical notes

- Working precision is 34 digits, round-half-even. `fix N` controls **display**
  only (0–16 places); stored values keep full precision.
- `?i`, `irr`, `xirr`, `byield`, `xyield` have no closed form. They are solved
  by scanning the rate axis for a sign change, then bisecting in decimal to
  convergence. Multiple IRRs are not all enumerated — the first bracket wins.
- `amort` uses end-of-period payments and works in magnitudes; round `pmt` (or
  solve `?pmt` at your display precision) if you want the schedule to close to
  exactly zero.
- `xnpv`/`xirr` fix the denominator at 365 (Excel's choice); `yearfrac` basis 1
  is ISDA actual/actual. The dated-bond price uses the SIA/Excel formula and
  reproduces the documented Microsoft `XNPV`, `XIRR`, `PRICE`, and `YIELD`
  worked examples to the displayed precision; basis 0 and basis 1 are the
  well-trodden paths.

A full user manual with the same material in print form is in
[`manual.pdf`](manual.pdf).

## License

GPL-3.0, consistent with MM-11 and MM-14. The vendored decNumber library is
under the ICU license (permissive), which GPL-3.0 permits redistributing
alongside.
