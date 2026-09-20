#define SDL_MAIN_USE_CALLBACKS 1  
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

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

const Uint64 target_fpns = (Uint64)(1e9 / TARGET_FPS);

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

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) {
        SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!SDL_CreateWindowAndRenderer(name, SDL_WINDOW_WIDTH, SDL_WINDOW_HEIGHT, SDL_WINDOW_RESIZABLE, &as->window, &as->renderer)) {
        SDL_Log("Cannot create window/renderer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetRenderLogicalPresentation(as->renderer, SDL_WINDOW_WIDTH, SDL_WINDOW_HEIGHT, SDL_LOGICAL_PRESENTATION_LETTERBOX);

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
	  SDL_Log("Inside!");
          as->inside = true;
      }
      SDL_Log("\n");
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
}
