/*
    tg
    Copyright (C) 2015 Marcello Mamino

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2 as
    published by the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "stats.h"
#include <glib/gi18n.h>
#include <pthread.h>
#include <math.h>

#define STATS_CAP 36000   /* ~60 min a 10 ciclos/s */

static struct {
	pthread_mutex_t m;
	struct stats_point pts[STATS_CAP];
	int n, wp;
} st;

void stats_init(void)
{
	pthread_mutex_init(&st.m, NULL);
	st.n = st.wp = 0;
}

void stats_add(const struct stats_point *p)
{
	pthread_mutex_lock(&st.m);
	st.pts[st.wp] = *p;
	st.wp = (st.wp + 1) % STATS_CAP;
	if(st.n < STATS_CAP) st.n++;
	pthread_mutex_unlock(&st.m);
}

const char *position_name(int pos)
{
	/* Non-static: the initializer calls gettext at runtime. */
	const char *names[POSITION_CR + 1] = {
		_("none"), _("dial up"), _("dial down"), _("crown up"), _("crown down"),
		_("crown left"), _("crown right")
	};
	return pos >= POSITION_NONE && pos <= POSITION_CR ? names[pos] : "?";
}

static int stats_summary_filter(int pos, uint64_t window_ms,
                                int only_untagged, struct stats_summary *out)
{
	pthread_mutex_lock(&st.m);
	int start = st.n < STATS_CAP ? 0 : st.wp;
	uint64_t newest = st.n ? st.pts[(start + st.n - 1) % STATS_CAP].wall_ms : 0;
	double sum = 0, sq = 0, mn = 0, mx = 0, sb = 0, sa = 0;
	int cnt = 0, i;
	for(i = 0; i < st.n; i++) {
		const struct stats_point *p = &st.pts[(start + i) % STATS_CAP];
		if(only_untagged) {
			if(p->position != POSITION_NONE) continue;
		} else if(pos != POSITION_NONE && p->position != pos)
			continue;
		if(window_ms && p->wall_ms + window_ms < newest) continue;
		if(!cnt || p->rate < mn) mn = p->rate;
		if(!cnt || p->rate > mx) mx = p->rate;
		sum += p->rate;
		sq += p->rate * p->rate;
		sb += p->be;
		sa += p->amp;
		cnt++;
	}
	if(out) {
		out->n = cnt;
		if(cnt) {
			out->mean = sum / cnt;
			out->sigma = cnt > 1 ? sqrt((sq - cnt * out->mean * out->mean) / (cnt - 1)) : 0;
			out->min = mn;
			out->max = mx;
			out->mean_be = sb / cnt;
			out->mean_amp = sa / cnt;
		} else
			out->mean = out->sigma = out->min = out->max =
				out->mean_be = out->mean_amp = 0;
	}
	pthread_mutex_unlock(&st.m);
	return cnt;
}

int stats_summary(uint64_t window_ms, struct stats_summary *out)
{
	return stats_summary_filter(POSITION_NONE, window_ms, 0, out);
}

int stats_summary_pos(int pos, uint64_t window_ms, struct stats_summary *out)
{
	return stats_summary_filter(pos, window_ms, 0, out);
}

int stats_summary_untagged(uint64_t window_ms, struct stats_summary *out)
{
	return stats_summary_filter(POSITION_NONE, window_ms, 1, out);
}

int stats_get_range(uint64_t from_ms, struct stats_point *out, int max)
{
	pthread_mutex_lock(&st.m);
	int start = st.n < STATS_CAP ? 0 : st.wp;
	int cnt = 0, i;
	for(i = 0; i < st.n && cnt < max; i++) {
		const struct stats_point *p = &st.pts[(start + i) % STATS_CAP];
		if(p->wall_ms < from_ms) continue;
		out[cnt++] = *p;
	}
	pthread_mutex_unlock(&st.m);
	return cnt;
}

void stats_clear(void)
{
	pthread_mutex_lock(&st.m);
	st.n = st.wp = 0;
	pthread_mutex_unlock(&st.m);
}