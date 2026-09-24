#define SDL_MAIN_USE_CALLBACKS 1  
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_sensor.h>
// switch to SDL_gamepad if need a compatible application
//#include <SDL3/SDL_gamepad.h>

#define SDL_WINDOW_WIDTH 800
#define SDL_WINDOW_HEIGHT 600
#define TARGET_FPS 60
#define max_samples 128
#define SAMPLE_RATE 8000.0f
#define BRIEF_SOUND_LENGTH 0.125
#define SHORT_SOUND_LENGTH 0.25
#define LONG_SOUND_LENGTH 1
#define midPiano 49
#define A4 440.0f

#define WII_VENDOR_ID 0x057e
#define WII_REMOTE_ID 0x0306

static const char *name = "SDL Template";
static const char *version = "1";
static const char *appid = "io.damians-eng.demo";
static SDL_AudioDeviceID audio_device = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;

typedef struct
{
    SDL_Window *window;
    SDL_Renderer *renderer;
	SDL_AudioStream *stream;
    Uint64 last_time;
	bool clicked;
	bool inside;
} AppState;

typedef struct
{
	int axes;
	int buttons;
	int hats;
	bool accel;
	bool gyro;
	Sint16 *last_axes;
	bool *last_buttons;
	Uint8 *last_hats;
	float last_accel[3];
	float last_gyro[3];
	Uint64 last_sensor_print;
	Uint64 last_scan;
	bool first_sample;
} WiiState;
// only reading one at a time if placed here
static WiiState wii;
static SDL_Joystick *joystick = NULL;

const Uint64 target_fpns = (Uint64)(1e9 / TARGET_FPS);

static bool about_equal(float a, float b) {
	return SDL_fabsf(a - b) < 0.01f;
}

// ## Wii Specific Caching

static void free_cached_state(WiiState *wii)
{
    SDL_free(wii->last_axes);
    SDL_free(wii->last_buttons);
    SDL_free(wii->last_hats);

    wii->last_axes = NULL;
    wii->last_buttons = NULL;
    wii->last_hats = NULL;
}

static bool allocate_cached_state(WiiState *wii) {
	free_cached_state(wii);
	if (wii->axes > 0) {
		wii->last_axes = SDL_malloc((size_t)wii->axes * sizeof(*wii->last_axes));
		if (!wii->last_axes) {
			SDL_Log("Allocating axis state, %s\n", SDL_GetError()); return false;
		}
		int i;
		for(i = 0; i < wii->axes; ++i) {
			wii->last_axes[i] = (Sint16)0x7fff;
		}
	}
	if (wii->buttons > 0) {
		wii->last_buttons = SDL_malloc((size_t)wii->buttons * sizeof(*wii->last_buttons));
		if (!wii->last_buttons) {
			SDL_Log("Allocating button state, %s\n", SDL_GetError()); return false;
		}
		memset(wii->last_buttons, 0xff, (size_t)wii->buttons * sizeof(*wii->last_buttons));
	}
	if (wii->hats > 0) {
		wii->last_hats = SDL_malloc((size_t)wii->hats * sizeof(*wii->last_hats));
		if (!wii->last_hats) {
			SDL_Log("Allocating hat state, %s\n", SDL_GetError()); return false;
		}
		memset(wii->last_hats, 0xff, (size_t)wii->hats * sizeof(*wii->last_hats));
	}
	return true;
}

// ## end Wii Caching

static bool is_probably_wii_remote(SDL_JoystickID id) {
    return ((Uint16)SDL_GetJoystickVendorForID(id) == WII_VENDOR_ID) || 
		   ((Uint16)SDL_GetJoystickProductForID(id) == WII_REMOTE_ID);
}

static SDL_JoystickID find_wii_remote(void)
{
    int count = 0;
    SDL_JoystickID *ids = SDL_GetJoysticks(&count);

    if (!ids) {
        SDL_Log("Nothing found."); return 0;
    }

    SDL_JoystickID result = 0;

    for (int i = 0; i < count; ++i) {
        if (is_probably_wii_remote(ids[i])) {
            result = ids[i];
            break;
        }
    }

    SDL_free(ids);
    return result;
}

// App Init for Wii Remote

static bool open_wii(WiiState *wii, SDL_JoystickID id)
{
    memset(wii, 0, sizeof(*wii));
    joystick = SDL_OpenJoystick(id);
    if (!joystick) {
        SDL_Log("Problem on SDL_OpenJoystick, %s\n", SDL_GetError());
        return false;
    }

    wii->axes = SDL_GetNumJoystickAxes(joystick);
    wii->buttons = SDL_GetNumJoystickButtons(joystick);
    wii->hats = SDL_GetNumJoystickHats(joystick);

    const char *name = SDL_GetJoystickName(joystick);
    const char *path = SDL_GetJoystickPath(joystick);

    Uint16 vendor = SDL_GetJoystickVendor(joystick);
    Uint16 product = SDL_GetJoystickProduct(joystick);
    Uint16 version = SDL_GetJoystickProductVersion(joystick);

    SDL_Log("\n");
    SDL_Log("============================================================\n");
    SDL_Log("Wii Remote connected\n");
    SDL_Log("============================================================\n");
    SDL_Log("Name:       %s\n", name ? name : "(unknown)");
    SDL_Log("Path:       %s\n", path ? path : "(unknown)");
    SDL_Log("Instance:   %" SDL_PRIu32 "\n", SDL_GetJoystickID(joystick));
    SDL_Log("VID:PID:    %04x:%04x\n", vendor, product);
    SDL_Log("Version:    %04x\n", version);

    SDL_Log("\n");
    SDL_Log("SDL controls:\n");
    SDL_Log("  Axes:     %d\n", wii->axes);
    SDL_Log("  Buttons:  %d\n", wii->buttons);
    SDL_Log("  Hats:     %d\n", wii->hats);
    wii->accel =
        SDL_JoystickHasSensor(joystick, SDL_SENSOR_ACCEL);
    wii->gyro =
        SDL_JoystickHasSensor(joystick, SDL_SENSOR_GYRO);
    SDL_Log("\n");
    SDL_Log("Sensors:\n");
    SDL_Log("  Accelerometer: %s\n", wii->accel ? "YES" : "NO");
    SDL_Log("  Gyroscope:     %s\n", wii->gyro ? "YES" : "NO");

    if (wii->accel) {
        if (!SDL_SetJoystickSensorEnabled(
                joystick, SDL_SENSOR_ACCEL, true)) {
            SDL_Log("Enabling accelerometer: %s\n", SDL_GetError());
        } else {
            SDL_Log(" - Accelerometer enabled\n");
        }
    }
    if (wii->gyro) {
        if (!SDL_SetJoystickSensorEnabled(
                joystick, SDL_SENSOR_GYRO, true)) {
            SDL_Log("Enabling gyroscope: %s\n", SDL_GetError());
        } else {
            SDL_Log(" - Gyroscope enabled\n");
        }
    }

    if (!allocate_cached_state(wii)) {
        SDL_CloseJoystick(joystick);
        joystick = NULL;
        return false;
    }

    SDL_Log("\nOutput tests:\n");

    if (SDL_RumbleJoystick(joystick,
                           0xffff,
                           0,
                           250)) {
        SDL_Log("  Rumble:       YES (250 ms test)\n");
    } else {
        SDL_Log("  Rumble:       NO / unsupported\n");
    }

    if (SDL_SetJoystickLED(joystick, 0, 0, 255)) {
        SDL_Log("  LED:          YES (blue test)\n");
    } else {
        SDL_Log("  LED:          NO / unsupported\n");
    }

    SDL_Log("\n");
    SDL_Log("Move buttons/sticks and the Remote.\n");
    SDL_Log("Attach/detach an extension while this program is running.\n");
    SDL_Log("Press Escape to quit.\n\n");

    wii->first_sample = true;
    return true;
}

// Use in App Close

static void close_wii(WiiState *wii)
{
    free_cached_state(wii);
    if (joystick) {
        SDL_RumbleJoystick(joystick, 0, 0, 0);
        SDL_CloseJoystick(joystick);
        joystick = NULL;
    }
}

//
// ## Reports
//

static void print_buttons(WiiState *wii)
{
    for (int i = 0; i < wii->buttons; ++i) {
        bool state = SDL_GetJoystickButton(joystick, i);
        if (wii->first_sample || state != wii->last_buttons[i]) {
            SDL_Log("BUTTON[%02d] = %s\n",
                   i,
                   state ? "DOWN" : "UP");

            wii->last_buttons[i] = state;
        }
    }
}


static void print_axes(WiiState *wii)
{
    for (int i = 0; i < wii->axes; ++i) {
        Sint16 state = SDL_GetJoystickAxis(joystick, i);
        if (wii->first_sample ||
            SDL_abs((int)state - (int)wii->last_axes[i]) > 256) {
            SDL_Log("AXIS[%02d]   = %6d (%+.3f)\n",
                   i,
                   state,
                   (double)state / 32767.0);
            wii->last_axes[i] = state;
        }
    }
}


static void print_hats(WiiState *wii)
{
    for (int i = 0; i < wii->hats; ++i) {
        Uint8 state = SDL_GetJoystickHat(joystick, i);
        if (wii->first_sample || state != wii->last_hats[i]) {
            SDL_Log("HAT[%02d]    = (0x%02x)\n",
                   i,
                   //(char)state,
                   state);
            wii->last_hats[i] = state;
        }
    }
}


static void print_sensor_values(void *appstate, WiiState *wii)
{
	AppState *as = (AppState *)appstate;
	const int sensor_print_ms = 100;
    if (as->last_time - wii->last_sensor_print < sensor_print_ms) {
        return;
    }
    wii->last_sensor_print = as->last_time;

    bool print = false;

    float accel[3];
    float gyro[3];

    if (wii->accel &&
        SDL_GetJoystickSensorData(joystick, SDL_SENSOR_ACCEL, accel, 3)) {
        if (wii->first_sample ||
            !about_equal(accel[0], wii->last_accel[0]) ||
            !about_equal(accel[1], wii->last_accel[1]) ||
            !about_equal(accel[2], wii->last_accel[2])) {
			print = true;
            memcpy(wii->last_accel, accel, sizeof(wii->last_accel));
        }
    }

    if (wii->gyro &&
        SDL_GetJoystickSensorData(joystick, SDL_SENSOR_GYRO, gyro, 3)) {
        if (wii->first_sample ||
            !about_equal(gyro[0], wii->last_gyro[0]) ||
            !about_equal(gyro[1], wii->last_gyro[1]) ||
            !about_equal(gyro[2], wii->last_gyro[2])) {
            print = true;
            memcpy(wii->last_gyro, gyro, sizeof(wii->last_gyro));
        }
    }

    if (!print) {
        return;
    }

    if (wii->accel) {
        SDL_Log("ACCEL       = %+7.3f %+7.3f %+7.3f m/s^2\n",
               (double)accel[0],
               (double)accel[1],
               (double)accel[2]);
    }

    if (wii->gyro) {
        SDL_Log("GYRO        = %+7.3f %+7.3f %+7.3f rad/s\n",
               (double)gyro[0],
               (double)gyro[1],
               (double)gyro[2]);
    }
}


static void poll_wii(void *appstate, WiiState *wii)
{
    AppState *as = (AppState *)appstate;
    SDL_UpdateJoysticks();

    print_buttons(wii);
    print_axes(wii);
    print_hats(wii);
    print_sensor_values(as, wii);

    if (wii->first_sample) {
        SDL_Log("\n");
        wii->first_sample = false;
    }
}

//
// ## End Reports
//

static int current_sine_sample = 0;
//static int freq = 240;
static int piano_key = midPiano;
static int pulse_samples_remaining = 0;

static float PianoKeyToFrequency(int key) {
    return A4 * SDL_powf(2.0f, (key - midPiano) / 12.0f);
    // 440 * 2^((key - 49)/12) because each of the 9 octaves have 12 possible notes
    // the possible returns are between 16 and 8000 Hz
    // Use the table at https://muted.io/note-frequencies/ for reference
}

static void SDLCALL FeedTheAudioStreamMore(void *userdata, SDL_AudioStream *astream, int additional_amount, int total_amount)
{
    additional_amount /= sizeof (float);  /* convert to samples */
    while (additional_amount > 0) {
        float samples[max_samples];  /* feed each iteration until we have enough. */
        int total = SDL_min(additional_amount, SDL_arraysize(samples));
	// potential FIXME? adjust the modulus and the number of times to feed in sample at a time depending on AudioSpec.channels
	// for stereo
	if (total % 2 != 0) {
	    total--;
	}
	if (total == 0) break;
        int i;

        for (i = 0; i < total; ){
	    if (pulse_samples_remaining > 0) {
              //const float phase = current_sine_sample * freq / SAMPLE_RATE;
	      const float frequency = PianoKeyToFrequency(piano_key);
	      const float phase = current_sine_sample * frequency / SAMPLE_RATE;
	      const float sample_value = SDL_sinf(phase * 2 * SDL_PI_F);
	      // for stereo
              samples[i++] = sample_value; samples[i++] = sample_value;
              current_sine_sample++;
	      pulse_samples_remaining--;
	    }
	    else {
	      samples[i++] = 0.0f; samples[i++] = 0.0f;
	    }
        }

        /* wrapping around to avoid floating-point errors */
        current_sine_sample %= (int)SAMPLE_RATE;

        SDL_PutAudioStreamData(astream, samples, total * sizeof (float));
        additional_amount -= total; 
    }
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{

    AppState *as = (AppState *)SDL_calloc(1, sizeof(AppState));
    if (!as) {
        SDL_Log("something wrong with appstate: %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    *appstate = as;

    if(!SDL_SetAppMetadata(name, version, appid)) {
        SDL_Log("Some problem setting metadata: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS | SDL_INIT_JOYSTICK)) {
        SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!SDL_CreateWindowAndRenderer(name, SDL_WINDOW_WIDTH, SDL_WINDOW_HEIGHT, SDL_WINDOW_RESIZABLE, &as->window, &as->renderer)) {
        SDL_Log("Cannot create window/renderer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetRenderLogicalPresentation(as->renderer, SDL_WINDOW_WIDTH, SDL_WINDOW_HEIGHT, SDL_LOGICAL_PRESENTATION_LETTERBOX);

	memset(&wii, 0, sizeof(wii));
	
    SDL_AudioSpec spec;
    spec.channels = 2;
    spec.format = SDL_AUDIO_F32;
    spec.freq = SAMPLE_RATE;
    as->stream = SDL_OpenAudioDeviceStream(audio_device, &spec, FeedTheAudioStreamMore, NULL);
    if (!as->stream) {
        SDL_Log("Audio stream error: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    /* SDL_OpenAudioDeviceStream starts the device paused. You have to tell it to start! */
    SDL_ResumeAudioStreamDevice(as->stream);

    as->last_time = SDL_GetTicksNS();
    return SDL_APP_CONTINUE; 
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{

  AppState *as = (AppState *)appstate;
  switch (event->type) {
    case SDL_EVENT_QUIT:
      return SDL_APP_SUCCESS;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
      return SDL_APP_SUCCESS;
    case SDL_EVENT_KEY_DOWN:
      if (event->key.key == SDLK_ESCAPE) {
          return SDL_APP_SUCCESS;
      }
      break;
    case SDL_EVENT_MOUSE_WHEEL:
      float wheelMove = event->wheel.y;
      //freq += wheelMove;
      piano_key += wheelMove;
      if (piano_key < 1) { piano_key = 1;}
      if (piano_key > 88) { piano_key = 88;}
      break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
      as->clicked = event->button.down;
      if ((event->button.button == SDL_BUTTON_LEFT) && (as->inside)) {
          pulse_samples_remaining = (int)(SAMPLE_RATE * BRIEF_SOUND_LENGTH);
      } 
      break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
      as->clicked = event->button.down;
      break;
    case SDL_EVENT_MOUSE_MOTION:
      //SDL_Log("Current mouse position is: (%f, %f)", event->motion.x, event->motion.y);
      as->inside = false;
      int beginx = ((SDL_WINDOW_WIDTH / 2) - (100 / 2));
      int endx = ((SDL_WINDOW_WIDTH / 2) + (100 / 2));
      int beginy = ((SDL_WINDOW_HEIGHT/ 2) - (200 / 2));
      int endy = ((SDL_WINDOW_HEIGHT/ 2) + (200 / 2));
      /*
      if ( beginx < event->motion.x ) {SDL_Log("Inside top left. ");}
      if ( event->motion.x < endx ) {SDL_Log("Inside top right. ");}
      if ( beginy < event->motion.y ) {SDL_Log("Inside bottom left. ");}
      if ( event->motion.y < endy ) {SDL_Log("Inside bottom right. ");}
      */
      if ( 
	    ( beginx < event->motion.x ) && 
	    ( event->motion.x < endx ) && 
            ( beginy < event->motion.y) &&
	    ( event->motion.y < endy)) {
	  //SDL_Log("Inside!");
          as->inside = true;
      }
      //SDL_Log("\n");
      break;
	case SDL_EVENT_JOYSTICK_ADDED:
		SDL_Log("Joystick added: %u\n", event->jdevice.which);
		if(!joystick && is_probably_wii_remote(event->jdevice.which)) {
			if(!open_wii(&wii, event->jdevice.which)) {
				SDL_Log("Failed to open Remote: %s\n", SDL_GetError());
			}
		}
	  break;
	case SDL_EVENT_JOYSTICK_REMOVED:
		if (joystick && event->jdevice.which == SDL_GetJoystickID(joystick)) {
			close_wii(&wii);
			SDL_Log("Remote disconnected.\n");
		}
		break;
    default:
      return SDL_APP_CONTINUE;
  }
  return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    AppState *as = (AppState *)appstate;
    const Uint64 now = SDL_GetTicksNS();
    const Uint64 elapsed = now - as->last_time;
	if (joystick) {
		if (!allocate_cached_state(&wii)) {
			SDL_Log("Unable to resize input state\n"); close_wii(&wii);
		} else {
			wii.first_sample = true;
		}
		poll_wii(as, &wii);
	}
    if (elapsed >= target_fpns) {
        as->last_time = now;
        SDL_SetRenderDrawColor(as->renderer, 30, 30, 30, SDL_ALPHA_OPAQUE);
        SDL_RenderClear(as->renderer);
    
        SDL_SetRenderDrawColor(as->renderer, 30, 100, 200, SDL_ALPHA_OPAQUE);
        const int rect_width = 100;
        const int rect_height = 200;
        SDL_FRect rect = {
            // Place the rectangle in the middle of the window
                ((SDL_WINDOW_WIDTH / 2) - (rect_width / 2)),
                ((SDL_WINDOW_HEIGHT / 2) - (rect_height / 2)),
                rect_width,
                rect_height
        };
        SDL_RenderRect(as->renderer, &rect);
        if (as->inside) {SDL_RenderFillRect(as->renderer, &rect);}
        if (as->clicked && as->inside) {
            SDL_SetRenderDrawColor(as->renderer, 200, 100, 0, SDL_ALPHA_OPAQUE);
            SDL_RenderFillRect(as->renderer, &rect);
        }
        SDL_RenderPresent(as->renderer);
    } else {
        const Uint64 remaining = target_fpns - elapsed;
        SDL_DelayNS(remaining);
    }
    return SDL_APP_CONTINUE; 
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
	close_wii(&wii);
}
