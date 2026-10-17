// Standalone test for the Gregorian date algorithms used by datecalc.cpp.
// Covers: leap/dim, validity, JDN, weekday, day-of-year and the JDN->date
// inverse (used by "Add Days"). Compile: g++ -std=c++17 -O2 datecalc_test.cpp
#include <stdio.h>
#include <string.h>

static bool is_leap(int y) { return (y % 4 == 0) && (y % 100 != 0 || y % 400 == 0); }
static int dim(int y, int m) {
    static const int md[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 2 && is_leap(y)) return 29;
    return (m >= 1 && m <= 12) ? md[m - 1] : 0;
}
static bool valid(int y, int m, int d) {
    if (y < 1 || y > 9999 || m < 1 || m > 12) return false;
    return d >= 1 && d <= dim(y, m);
}
static long jdn(int y, int m, int d) {
    int a = (14 - m) / 12;
    int yy = y + 4800 - a;
    int mm = m + 12 * a - 3;
    return (long)d + (153L * mm + 2) / 5 + 365L * yy + yy / 4 - yy / 100 + yy / 400 - 32045L;
}
static void jdn_to_date(long j, int* y, int* m, int* d) {
    long a = j + 32044;
    long b = (4 * a + 3) / 146097;
    long c = a - 146097 * b / 4;
    long dd = (4 * c + 3) / 1461;
    long e = c - 1461 * dd / 4;
    long mm = (5 * e + 2) / 153;
    *d = (int)(e - (153 * mm + 2) / 5 + 1);
    *m = (int)(mm + 3 - 12 * (mm / 10));
    *y = (int)(100 * b + dd - 4800 + mm / 10);
}
static int weekday_from_jdn(long j) { return (int)((j + 1) % 7); }  // 0=Sun
static int day_of_year(int y, int m, int d) {
    int sum = 0;
    for (int i = 1; i < m; i++) sum += dim(y, i);
    return sum + d;
}

static int failures = 0;
static void check_i(const char* what, long long got, long long expect) {
    if (got != expect) { printf("FAIL %-46s got=%lld expect=%lld\n", what, got, expect); failures++; }
}
static void check_date(const char* what, long j, int ey, int em, int ed) {
    int y, m, d;
    jdn_to_date(j, &y, &m, &d);
    if (y != ey || m != em || d != ed) {
        printf("FAIL %-46s j=%ld got=%d-%02d-%02d expect=%d-%02d-%02d\n",
               what, j, y, m, d, ey, em, ed);
        failures++;
    }
}

int main() {
    // JDN of known dates (reference values from standard Julian Day formulas)
    check_i("JDN 2000-01-01", jdn(2000,1,1), 2451545);
    check_i("JDN 2026-01-01", jdn(2026,1,1), 2461042);   // 2451545 + 26*365 + 7 leap days
    check_i("JDN 1970-01-01", jdn(1970,1,1), 2440588);
    check_i("JDN 1900-01-01", jdn(1900,1,1), 2415021);
    // known weekday anchors: 2000-01-01 = Saturday (6), 1970-01-01 = Thursday (4)
    check_i("weekday 2000-01-01 (Sat=6)", weekday_from_jdn(jdn(2000,1,1)), 6);
    check_i("weekday 1970-01-01 (Thu=4)", weekday_from_jdn(jdn(1970,1,1)), 4);
    check_i("weekday 2026-10-10 (Sat=6)", weekday_from_jdn(jdn(2026,10,10)), 6);
    check_i("weekday 2026-01-01 (Thu=4)", weekday_from_jdn(jdn(2026,1,1)), 4);
    check_i("weekday 2024-02-29 (Thu=4)", weekday_from_jdn(jdn(2024,2,29)), 4);
    // diff: 2026-10-10 - 2026-01-01 = 282 (matches the GUI test)
    check_i("diff 2026-10-10..2026-01-01", jdn(2026,10,10) - jdn(2026,1,1), 282);
    // inverse: JDN -> date roundtrips
    check_date("inv 2000-01-01", jdn(2000,1,1), 2000,1,1);
    check_date("inv 1970-01-01", jdn(1970,1,1), 1970,1,1);
    check_date("inv 2026-10-10", jdn(2026,10,10), 2026,10,10);
    check_date("inv 2024-02-29", jdn(2024,2,29), 2024,2,29);
    check_date("inv 2100-03-01", jdn(2100,3,1), 2100,3,1);
    check_date("inv 1-01-01", jdn(1,1,1), 1,1,1);
    check_date("inv 9999-12-31", jdn(9999,12,31), 9999,12,31);
    // add days: 2026-01-01 + 282 = 2026-10-10 ; + 365 = 2027-01-01 ; + 1 = 2026-01-02
    check_date("add 282 to 2026-01-01", jdn(2026,1,1) + 282, 2026,10,10);
    check_date("add 365 to 2026-01-01", jdn(2026,1,1) + 365, 2027,1,1);
    check_date("add 1 to 2026-01-01", jdn(2026,1,1) + 1, 2026,1,2);
    check_date("sub 1 from 2026-01-01", jdn(2026,1,1) - 1, 2025,12,31);
    // day of year
    check_i("doy 2026-01-01", day_of_year(2026,1,1), 1);
    check_i("doy 2026-10-10", day_of_year(2026,10,10), 283);
    check_i("doy 2026-12-31", day_of_year(2026,12,31), 365);
    check_i("doy 2024-12-31 (leap)", day_of_year(2024,12,31), 366);
    // validity
    check_i("valid 2026-02-28", valid(2026,2,28), 1);
    check_i("invalid 2026-02-29", valid(2026,2,29), 0);
    check_i("valid 2024-02-29 (leap)", valid(2024,2,29), 1);
    check_i("invalid 2026-13-01", valid(2026,13,1), 0);
    check_i("invalid 2026-00-10", valid(2026,0,10), 0);
    check_i("invalid 2026-04-31", valid(2026,4,31), 0);
    check_i("leap 2000", is_leap(2000), 1);
    check_i("leap 1900", is_leap(1900), 0);
    check_i("leap 2024", is_leap(2024), 1);
    check_i("leap 2026", is_leap(2026), 0);
    if (failures == 0) printf("ALL PASS\n");
    else printf("%d FAILURES\n", failures);
    return failures ? 1 : 0;
}
