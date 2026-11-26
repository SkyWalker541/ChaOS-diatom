/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Fast-forward's hold and release of the CPU's clock (port/port_cpufreq.h),
 * against a folder standing in for sysfs.
 *
 * What changed: ChaOS's Performance setting (chaos src/cpumode.h) sets a
 * floor for a game, and the release after fast-forward put the device's
 * lowest back instead, undoing it until the game ended. Released now, the
 * floor the launcher wrote is what goes back; with none, the lowest as
 * before. */
#define DIATOM_CPUFREQ       "build/cpufreq_sys/"
#define DIATOM_CPUFREQ_BOOST "build/cpufreq_sys/boost"
#define DIATOM_CPUFREQ_FLOOR "build/cpufreq_sys/floor"
#include "../port/port_cpufreq.h"

#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;

static void put(const char *name, const char *v)
{
	char p[128];
	FILE *f;

	snprintf(p, sizeof p, DIATOM_CPUFREQ "%s", name);
	if ((f = fopen(p, "w"))) { fputs(v, f); fclose(f); }
}

static void expect(const char *name, const char *want, const char *what)
{
	char p[128], got[32] = "";
	FILE *f;

	snprintf(p, sizeof p, DIATOM_CPUFREQ "%s", name);
	if ((f = fopen(p, "r"))) { if (!fgets(got, sizeof got, f)) got[0] = '\0'; fclose(f); }
	got[strcspn(got, "\n")] = '\0';
	if (strcmp(got, want)) { fprintf(stderr, "cpufreq: %s: got %s, want %s\n", what, got, want); fails++; }
	else printf("  ok   %s\n", what);
}

int main(void)
{
	mkdir("build", 0755);
	mkdir("build/cpufreq_sys", 0755);
	put("cpuinfo_min_freq", "408000\n");
	put("cpuinfo_max_freq", "2000000\n");
	put("scaling_min_freq", "1008000\n");
	unlink(DIATOM_CPUFREQ_FLOOR);

	diatom_cpufreq_hold_top(true, false);
	expect("scaling_min_freq", "2000000", "fast-forward holds the top");
	put("floor", "1008000\n");
	diatom_cpufreq_hold_top(false, false);
	expect("scaling_min_freq", "1008000", "released onto the launcher's floor");
	unlink(DIATOM_CPUFREQ_FLOOR);
	diatom_cpufreq_hold_top(true, false);
	diatom_cpufreq_hold_top(false, false);
	expect("scaling_min_freq", "408000", "with no floor, onto the device's lowest as before");
	put("floor", "junk\n");
	diatom_cpufreq_hold_top(true, false);
	diatom_cpufreq_hold_top(false, false);
	expect("scaling_min_freq", "408000", "a floor that isn't a number is not used");
	unlink(DIATOM_CPUFREQ_FLOOR);
	if (fails) return 1;
	printf("cpu clock: ok\n");
	return 0;
}
