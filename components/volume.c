/* See LICENSE file for copyright and license details. */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "../util.h"

#if defined(__OpenBSD__)
	#include <sys/audioio.h>

	const char *
	vol_perc(const char *card)
	{
		static int cls = -1;
		mixer_devinfo_t mdi;
		mixer_ctrl_t mc;
		int afd = -1, m = -1, v = -1;

		if ((afd = open(card, O_RDONLY)) < 0) {
			warn("open '%s':", card);
			return NULL;
		}

		for (mdi.index = 0; cls == -1; mdi.index++) {
			if (ioctl(afd, AUDIO_MIXER_DEVINFO, &mdi) < 0) {
				warn("ioctl 'AUDIO_MIXER_DEVINFO':");
				close(afd);
				return NULL;
			}
			if (mdi.type == AUDIO_MIXER_CLASS &&
			    !strncmp(mdi.label.name,
				     AudioCoutputs,
				     MAX_AUDIO_DEV_LEN))
				cls = mdi.index;
			}
		for (mdi.index = 0; v == -1 || m == -1; mdi.index++) {
			if (ioctl(afd, AUDIO_MIXER_DEVINFO, &mdi) < 0) {
				warn("ioctl 'AUDIO_MIXER_DEVINFO':");
				close(afd);
				return NULL;
			}
			if (mdi.mixer_class == cls &&
			    ((mdi.type == AUDIO_MIXER_VALUE &&
			      !strncmp(mdi.label.name,
				       AudioNmaster,
				       MAX_AUDIO_DEV_LEN)) ||
			     (mdi.type == AUDIO_MIXER_ENUM &&
			      !strncmp(mdi.label.name,
				      AudioNmute,
				      MAX_AUDIO_DEV_LEN)))) {
				mc.dev = mdi.index, mc.type = mdi.type;
				if (ioctl(afd, AUDIO_MIXER_READ, &mc) < 0) {
					warn("ioctl 'AUDIO_MIXER_READ':");
					close(afd);
					return NULL;
				}
				if (mc.type == AUDIO_MIXER_VALUE)
					v = mc.un.value.num_channels == 1 ?
					    mc.un.value.level[AUDIO_MIXER_LEVEL_MONO] :
					    (mc.un.value.level[AUDIO_MIXER_LEVEL_LEFT] >
					     mc.un.value.level[AUDIO_MIXER_LEVEL_RIGHT] ?
					     mc.un.value.level[AUDIO_MIXER_LEVEL_LEFT] :
					     mc.un.value.level[AUDIO_MIXER_LEVEL_RIGHT]);
				else if (mc.type == AUDIO_MIXER_ENUM)
					m = mc.un.ord;
			}
		}

		close(afd);

		return bprintf("%d", m ? 0 : v * 100 / 255);
	}
#else

#include <alsa/asoundlib.h>

static int active;
static int master;

int
mixer_elem_cb(snd_mixer_elem_t *elem, unsigned int mask)
{
  long min, max, vol;
  int r;

  r = snd_mixer_selem_get_playback_switch(elem, SND_MIXER_SCHN_UNKNOWN, &active);
  if (r < 0) {
    fprintf(stderr,"snd_mixer_selem_get_playback_switch: %s",
        snd_strerror(r));
    return -1;
  }
  r = snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
  if (r < 0) {
    fprintf(stderr,"snd_mixer_selem_get_playback_volume_range: %s",
        snd_strerror(r));
    return -1;
  }
  r = snd_mixer_selem_get_playback_volume(elem,
      SND_MIXER_SCHN_UNKNOWN, &vol);
  if (r < 0) {
    fprintf(stderr,"snd_mixer_selem_get_playback_volume: %s",
        snd_strerror(r));
    return -1;
  }
  /* compute percentage */
  vol -= min;
  max -= min;
  if (max == 0)
    master = 0;
  else
    master = 100 * vol / max;
  return 0;
}

const char *vol_perc(const char *card)
{
  snd_mixer_selem_id_t *id;
  snd_mixer_elem_t *elem;
  static snd_mixer_t *mixerp;
  struct pollfd pfd[1];
  int r;

  snd_mixer_selem_id_alloca(&id);
  snd_mixer_selem_id_set_name(id, "Master");
  snd_mixer_selem_id_set_index(id, 0);

  if (mixerp != NULL)
    goto readvol;

  r = snd_mixer_open(&mixerp, O_RDONLY);
  if (r < 0) {
    fprintf(stderr,"snd_mixer_open: %s", snd_strerror(r));
    return "";
  }
  r = snd_mixer_attach(mixerp, card);
  if (r < 0) {
    fprintf(stderr,"snd_mixer_attach: %s", snd_strerror(r));
    goto out;
  }
  r = snd_mixer_selem_register(mixerp, NULL, NULL);
  if (r < 0) {
    fprintf(stderr,"snd_mixer_selem_register: %s", snd_strerror(r));
    goto out;
  }
  r = snd_mixer_load(mixerp);
  if (r < 0) {
    fprintf(stderr,"snd_mixer_load: %s", snd_strerror(r));
    goto out;
  }
  elem = snd_mixer_find_selem(mixerp, id);
  if (elem == NULL) {
    fprintf(stderr,"could not find mixer element");
    goto out;
  }
  snd_mixer_elem_set_callback(elem, mixer_elem_cb);
  /* force the callback the first time around */
  r = mixer_elem_cb(elem, 0);
  if (r < 0)
    goto out;
readvol:
  r = snd_mixer_poll_descriptors(mixerp, pfd, LEN(pfd));
  if (r < 0) {
    fprintf(stderr,"snd_mixer_poll_descriptors: %s", snd_strerror(r));
    goto out;
  }
  r = snd_mixer_handle_events(mixerp);
  if (r < 0) {
    fprintf(stderr, "snd_mixer_handle_events: %s", snd_strerror(r));
    goto out;
  }
  if (active)
		return bprintf("%d%%", master);
  else
		return bprintf("!%d%%", master);

  return "";
out:
  snd_mixer_free(mixerp);
  snd_mixer_close(mixerp);
  mixerp = NULL;
  return "";
}
#endif
