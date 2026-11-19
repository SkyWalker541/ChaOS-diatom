/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CHAOS_LOGSTAMP_H
#define CHAOS_LOGSTAMP_H

/* ChaOS: a time at the start of every line this process writes to stdout and
 * stderr - the clock, and the seconds since the device came on, as
 * "[10:35:12.345 +123.4s] " - so a log can be matched to when something
 * happened. The clock is wrong until Wi-Fi sets it; the seconds never are.
 *
 * Each piece is written straight to the file descriptor as it comes (no
 * buffer of its own), so a crash loses nothing that was printed. Safe to
 * call once, early in main; a second call does nothing. */
void logstamp_install(void);

#endif
