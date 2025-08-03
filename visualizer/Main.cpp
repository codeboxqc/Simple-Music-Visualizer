#include <Windows.h>
#undef max

#include <SDL2/SDL.h>
#include <SDL2/SDL2_gfxPrimitives.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <functional>
#include <string>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <iostream>
#include <comdef.h>
#include <vector>
#include <random>
#include <chrono>

#include "engine.h"

 int SCREEN_WIDTH = 900;
 int SCREEN_HEIGHT = 600;
const int NUM_BARS = 32;
const float TWO_PI = 2.0f * static_cast<float>(M_PI);

 
const float TIME_SLOWDOWN = 0.03f;  
const int PAR = 120 ;

struct Vector3D { float x, y, z; };
struct Vector2D { float x, y; Vector2D(float _x, float _y) : x(_x), y(_y) {} };

// Audio parameters for curve
struct AudioParams {
    float smoothedBass;
    float smoothedMid;
    float smoothedTreble;
    float smoothedAmplitude;
    bool beatDetected;
    float beatIntensity;
    float rotationSpeed;
    float globalAmplification;
};

inline float noise(float x, float y = 0.0f) {
    float dot = x * 12.9898f + y * 78.233f;
    float s = std::sinf(dot);
    return s - std::floor(s); // Fast and avoids redundant computation
}

inline float clamp(float v, float lo, float hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}


// HSB to RGB conversion
inline SDL_Color hsbToRgb(float hue, float saturation, float brightness, float alpha = 1.0f) {
    hue = std::fmod(hue, 360.0f);
    if (hue < 0.0f) hue += 360.0f;

    float s =  clamp(saturation, 0.0f, 100.0f) / 100.0f;
    float v =  clamp(brightness, 0.0f, 100.0f) / 100.0f;
    float a =  clamp(alpha, 0.0f, 1.0f);

    float c = v * s;
    float h = hue / 60.0f;
    float x = c * (1.0f - std::fabs(h - std::floor(h / 2.0f) * 2.0f - 1.0f));

    float r = 0.0f, g = 0.0f, b = 0.0f;

    int sector = static_cast<int>(h);
    switch (sector) {
    case 0: r = c; g = x; b = 0; break;
    case 1: r = x; g = c; b = 0; break;
    case 2: r = 0; g = c; b = x; break;
    case 3: r = 0; g = x; b = c; break;
    case 4: r = x; g = 0; b = c; break;
    case 5: r = c; g = 0; b = x; break;
    default: break; // should never happen if hue is within [0, 360)
    }

    float m = v - c;
    r += m;
    g += m;
    b += m;

    return SDL_Color{
        static_cast<Uint8>(std::round(r * 255.0f)),
        static_cast<Uint8>(std::round(g * 255.0f)),
        static_cast<Uint8>(std::round(b * 255.0f)),
        static_cast<Uint8>(std::round(a * 255.0f))
    };
}


// Particle system with enhanced effects
struct Particle {
    float x, y;
    float vx, vy;
    float life;
    float maxLife;
    float size;
    float rotation;
    float rotationSpeed;
    float scale;
    float scaleSpeed;
    float gravity;
    SDL_Color color;
    float frequency;
    float amplitude;
    enum class Shape { Circle, Rectangle, Star, Triangle, Pentagon, Hexagon } shape;
    enum class PaletteType { PLASMA, ICE, VOLCANO, ORANGE, MYSTIC, NEON, AURORA, FOREST, COSMIC, SUNSET } palette;

    Particle(float px, float py, SDL_Color c, Shape s, PaletteType p)
        : x(px), y(py), vx(0), vy(0), life(1.0f), maxLife(1.0f),
        size(2.0f), rotation(0.0f), rotationSpeed(0.0f), scale(1.0f), scaleSpeed(0.0f),
        gravity(0.0f), color(c), frequency(0.1f), amplitude(10.0f), shape(s), palette(p) {
    }

    void update(float audioLevel, float beat, float deltaTime, const std::vector<Particle>& particles, size_t selfIndex) {
        float soundForce = audioLevel * 100.0f;
        float beatForce = beat * 600.0f;

        float timeF = static_cast<float>(SDL_GetTicks()) * 0.002f;
        vx += (std::sin(frequency * timeF) + (static_cast<float>(rand()) / RAND_MAX - 0.5f) * audioLevel) * soundForce * 0.05f;
        vy += (std::cos(frequency * timeF) + (static_cast<float>(rand()) / RAND_MAX - 0.5f) * audioLevel) * soundForce * 0.05f;

        if (beat > 0.1f) {
            vx += (static_cast<float>(rand()) / RAND_MAX - 0.5f) * beatForce * 0.15f;
            vy += (static_cast<float>(rand()) / RAND_MAX - 0.5f) * beatForce * 0.15f;
            scale += 0.4f;
        }

        vx *= 0.98f;
        vy *= 0.98f;

        x += vx * deltaTime;
        y += vy * deltaTime;

        if (x - size / 2 < 0) {
            x = size / 2;
            vx = -vx * 0.9f;
        }
        else if (x + size / 2 > SCREEN_WIDTH) {
            x = SCREEN_WIDTH - size / 2;
            vx = -vx * 0.9f;
        }
        if (y - size / 2 < 0) {
            y = size / 2;
            vy = -vy * 0.9f;
        }
        else if (y + size / 2 > SCREEN_HEIGHT) {
            y = SCREEN_HEIGHT - size / 2;
            vy = -vy * 0.9f;
        }

        for (size_t i = 0; i < particles.size(); ++i) {
            if (i == selfIndex) continue;
            const auto& other = particles[i];
            float dx = x - other.x;
            float dy = y - other.y;
            float distance = std::sqrt(dx * dx + dy * dy);
            float minDistance = (size + other.size) / 2;

            if (distance < minDistance && distance > 0) {
                float angle = std::atan2(dy, dx);
                float overlap = minDistance - distance;
                float moveX = std::cos(angle) * overlap * 0.5f;
                float moveY = std::sin(angle) * overlap * 0.5f;

                x += moveX;
                y += moveY;

                float tempVx = vx;
                float tempVy = vy;
                vx = other.vx * 0.9f + (beat > 0.1f ? (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 75.0f : 0.0f);
                vy = other.vy * 0.9f + (beat > 0.1f ? (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 75.0f : 0.0f);
            }
        }

        life -= 0.005f * deltaTime * 60.0f;
        if (life < 0) life = 0;

        color.a = static_cast<Uint8>(life * 255.0f);
        size = 2.0f + audioLevel * 7.0f + beat * 5.0f;

        rotation += rotationSpeed * deltaTime;
        scale = (std::max)(0.5f, (std::min)(scale, 2.0f));
    }

    bool isAlive() const { return life > 0; }
};

// Wave pattern for background
struct Wave {
    float amplitude;
    float frequency;
    float phase;
    float speed;
    SDL_Color color;

    Wave(float amp, float freq, float ph, float sp, SDL_Color c)
        : amplitude(amp), frequency(freq), phase(ph), speed(sp), color(c) {
    }

    void update(float deltaTime) {
        phase += speed * deltaTime;
        if (phase > 2.0f * static_cast<float>(M_PI)) phase -= 2.0f * static_cast<float>(M_PI);
    }

    float getY(float x, float time, float audioMod = 1.0f) const {
        return amplitude * audioMod * std::sin(frequency * x + phase + time * 0.002f);
    }
};

class SimpleVisualizer {
private:
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* curveTexture; // Texture for curve rendering
    AudioEngine engine;

    std::vector<float> barHeights;
    std::vector<float> targetHeights;
    float backgroundIntensity;
    float wavePhase;
    bool running;

    std::vector<Particle> particles;
    std::vector<Wave> backgroundWaves;
    std::vector<std::vector<SDL_Color>> palettes = {
        {{200, 0, 255, 150}, {150, 0, 200, 150}, {100, 50, 255, 150}, {180, 0, 180, 150}},
        {{0, 191, 255, 150}, {135, 206, 235, 150}, {240, 248, 255, 150}, {70, 130, 180, 150}},
        {{255, 69, 0, 150}, {255, 140, 0, 150}, {220, 20, 60, 150}, {255, 99, 71, 150}},
        {{255, 165, 0, 150}, {255, 140, 0, 150}, {255, 215, 0, 150}, {255, 127, 80, 150}},
        {{138, 43, 226, 150}, {186, 85, 211, 150}, {147, 0, 211, 150}, {199, 21, 133, 150}},
        {{0, 255, 0, 150}, {255, 20, 147, 150}, {50, 205, 50, 150}, {255, 105, 180, 150}},
        {{0, 255, 127, 150}, {32, 178, 170, 150}, {64, 224, 208, 150}, {0, 206, 209, 150}},
        {{34, 139, 34, 150}, {107, 142, 35, 150}, {139, 69, 19, 150}, {85, 107, 47, 150}},
        {{25, 25, 112, 150}, {75, 0, 130, 150}, {106, 90, 205, 150}, {72, 61, 139, 150}},
        {{255, 182, 193, 150}, {255, 105, 180, 150}, {255, 69, 0, 150}, {186, 85, 211, 150}}
    };
    std::vector<float> colorPalettes = { 0.0f, 120.0f, 240.0f }; // HSB hues for curves
    std::mt19937 rng{ static_cast<unsigned int>(std::chrono::steady_clock::now().time_since_epoch().count()) };

    // Curve parameters
    float t = 0.0f;
    float param1 = 3.0f;
    float param2 = 2.0f;
    float baseScale = 1.0f;
    float minRadius = 50.0f;
    int width = SCREEN_WIDTH;
    int height = SCREEN_HEIGHT;
    int currentCurve = 0;
    AudioParams audioParams;

public:
    SimpleVisualizer() : window(nullptr), renderer(nullptr), curveTexture(nullptr), running(false), backgroundIntensity(0.0f), wavePhase(0.0f) {
        barHeights.resize(NUM_BARS, 0.0f);
        targetHeights.resize(NUM_BARS, 0.0f);

        for (int i = 0; i < 3; ++i) {
            float amp = 25.0f + i * 5.0f;
            float freq = 0.015f + i * 0.005f;
            float phase = static_cast<float>(M_PI) * i / 3.0f;
            float speed = 0.025f + i * 0.005f;
            auto palette = palettes[rng() % palettes.size()];
            SDL_Color color = palette[rng() % palette.size()];
            backgroundWaves.push_back(Wave(amp, freq, phase, speed, color));
        }
    }

    ~SimpleVisualizer() {
        cleanup();
    }

    bool initialize() {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
            std::cerr << "SDL Init failed: " << SDL_GetError() << std::endl;
            return false;
        }

       window = SDL_CreateWindow("Music Visualizer",
         SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
          SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN);

        /*
        window = SDL_CreateWindow("SynTheSia",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            0,
            0,
            SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        */

       


        SDL_GetWindowSize(window, &SCREEN_WIDTH, &SCREEN_HEIGHT);

 


        if (!window) {
            std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
            return false;
        }

        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
        if (!renderer) {
            std::cerr << "Renderer creation failed: " << SDL_GetError() << std::endl;
            return false;
        }

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

        // Create texture for curve rendering
        curveTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, SCREEN_WIDTH, SCREEN_HEIGHT);
        if (!curveTexture) {
            std::cerr << "Curve texture creation failed: " << SDL_GetError() << std::endl;
            return false;
        }
        SDL_SetTextureBlendMode(curveTexture, SDL_BLENDMODE_BLEND);

        if (!engine.initialize()) {
            std::cerr << "Audio engine init failed!" << std::endl;
            return false;
        }

        running = true;
        return true;
    }

  

    Vector2D calculateCurvePoint(float theta, float r, int type) {
        if (type == -1) type = currentCurve;

        

        float n3 = 0.5f + noise(TIME_SLOWDOWN / 10.0f + type * 2.0f);
        float tFactor = TIME_SLOWDOWN * (0.8f + 0.2f * noise(TIME_SLOWDOWN / 15.0f + type));

        float phi = M_PI / 4.0f; // fixed elevation for 3D
        Vector3D p3 = {
            r * std::sinf(phi) * std::cosf(theta),
            r * std::sinf(phi) * std::sinf(theta),
            r * std::cosf(phi)
        };

        // Apply modulation to create visual variation
        p3.x += 0.3f * r * std::cosf(theta * param1 + tFactor);
        p3.y += 0.3f * r * std::sinf(theta * param2 - tFactor);


        try {
            //float n3 = 0.5f + noise(t / 10 + type * 2);
            //float tFactor = t * (0.8f + 0.2f * noise(t / 15 + type));
            //Vector2D defaultPos(r * std::cosf(theta), r * std::sinf(theta));
           


            switch (type % 99) {
            case 0:
                return Vector2D(r * std::cosf(param1 * theta) * std::cosf(theta * n3 + t),
                    r * std::sinf(param2 * theta) * std::sinf(theta * 0.8f - t / 2));
            case 1: {
                float spiral = r * (0.6f + 0.3f * noise(theta / 8 + t));
                return Vector2D(spiral * std::cosf(theta) + r / 5 * std::cosf(9 * theta + t * 1.3f),
                    spiral * std::sinf(theta) - r / 5 * std::sinf(7 * theta - t * 1.1f));
            }
            case 2:
                return Vector2D(r * std::sinf(theta * param1 + t * 1.3f) * std::cosf(theta * param2 - t / 2),
                    r * std::cosf(theta * param2 - t * 1.7f) * std::sinf(theta * param1 + t / 3));
            case 3:
                return Vector2D(r * std::cosf(theta) * (1 + 0.25f * std::sinf(7 * theta + t * 3)),
                    r * std::sinf(theta) * (1 + 0.25f * std::cosf(5 * theta - t * 2)));
            case 4: {
                float vortex = r * (0.3f + 0.5f * std::powf(std::sinf(theta / 2 + t), 2));
                return Vector2D(vortex * std::cosf(theta + 5 * std::sinf(theta / 3 + t / 4)),
                    vortex * std::sinf(theta + 5 * std::cosf(theta / 4 - t / 5)));
            }
            case 5:
                return Vector2D(r * std::sinf(3 * theta + t * 1.2f) * std::cosf(2 * theta - t * 0.7f),
                    r * std::cosf(4 * theta - t * 0.9f) * std::sinf(5 * theta + t * 1.1f));

            case 6: {
                float branch = r * (0.4f + 0.1f * noise(theta * 5 + t));
                return Vector2D(branch * std::cosf(theta) * (1 + 0.3f * std::sinf(13 * theta + t * 2)),
                    branch * std::sinf(theta) * (1 + 0.3f * std::cosf(11 * theta - t * 1.5f)));
            }
            case 7: {
                float orbital = r * (0.7f + 0.2f * std::sinf(theta * 2 + t * 3));
                return Vector2D(orbital * std::cosf(theta + std::sinf(theta * 7 + t * 2)),
                    orbital * std::sinf(theta + std::cosf(theta * 6 - t * 1.8f)));
            }
            case 8:
                return Vector2D(r * std::tanf(theta * param1 * 0.2f + t) * std::cosf(theta * param2 * 0.3f),
                    r * std::tanf(theta * param2 * 0.25f - t) * std::sinf(theta * param1 * 0.35f));
            case 9: {
                float flux = r * (0.5f + 0.3f * std::atanf(std::sinf(theta * 3 + t)));
                return Vector2D(flux * std::cosf(theta) + r / 4 * std::logf(std::fabs(5 * theta + t)),
                    flux * std::sinf(theta) - r / 4 * std::logf(std::fabs(4 * theta - t)));
            }
            case 10:
                return Vector2D(r * std::powf(std::sinf(theta * 0.7f + t), 3) * std::cosf(theta * 2),
                    r * std::powf(std::cosf(theta * 0.8f - t), 3) * std::sinf(theta * 2.5f));
            case 11: {
                float bio = r * (0.4f + 0.2f * (std::expf(std::sinf(theta + t)) - 0.5f));
                return Vector2D(bio * std::cosf(theta) * (1 + 0.4f * std::sinf(17 * theta + t * 4)),
                    bio * std::sinf(theta) * (1 + 0.4f * std::cosf(19 * theta - t * 3)));
            }
            case 12: {
                float well = r * (0.3f + 0.6f / (1 + 0.5f * std::fabs(theta - M_PI + t)));
                return Vector2D(well * std::cosf(theta + t / 3), well * std::sinf(theta - t / 4));
            }
            case 13:
                return Vector2D(r * std::sinf(theta + t) * std::cosf(2 * theta + t / 2) * (1 + 0.2f * std::sinf(23 * theta)),
                    r * std::cosf(theta - t) * std::sinf(3 * theta - t / 3) * (1 + 0.2f * std::cosf(19 * theta)));
            case 14:
                return Vector2D(r * std::powf(std::cosf(theta * param1), 2) * std::cosf(3 * theta + tFactor),
                    r * std::powf(std::sinf(theta * param2), 2) * std::sinf(2 * theta - tFactor));
            case 15: {
                float torus = r * (0.5f + 0.3f * std::sinf(theta * 4 + tFactor * 2));
                return Vector2D(torus * std::cosf(theta) + r / 3 * std::sinf(theta * 7 + tFactor),
                    torus * std::sinf(theta) - r / 3 * std::cosf(theta * 5 - tFactor));
            }
            case 16: {
                float fib = r * (0.4f + 0.1f * std::fmodf(theta, TWO_PI) / M_PI);
                return Vector2D(fib * std::cosf(theta + tFactor) * (1 + 0.2f * std::sinf(13 * theta)),
                    fib * std::sinf(theta - tFactor) * (1 + 0.2f * std::cosf(11 * theta)));
            }
            case 17:
                return Vector2D(r * (std::cosf(theta * param1) + 0.3f * noise(theta * 10 + tFactor)) * std::cosf(theta + t / 5),
                    r * (std::sinf(theta * param2) + 0.3f * noise(theta * 12 - tFactor)) * std::sinf(theta - t / 7));
            case 18: {
                float nebula = r * (0.6f + 0.2f * std::powf(std::sinf(theta * 0.7f + tFactor * 0.3f), 3));
                return Vector2D(nebula * std::cosf(theta) * (1 + 0.4f * std::tanf(theta * 3 + tFactor * 0.5f)),
                    nebula * std::sinf(theta) * (1 + 0.4f * std::tanf(theta * 4 - tFactor * 0.6f)));
            }
            case 19:
                return Vector2D(r * std::sinf(theta * param1) * std::cosf(theta * param2 + tFactor) * (1 + 0.2f * std::sinf(17 * theta)),
                    r * std::cosf(theta * param2) * std::sinf(theta * param1 - tFactor) * (1 + 0.2f * std::cosf(19 * theta)));
            case 20:
                return Vector2D(r * std::tanf(theta * 0.3f + tFactor * 0.2f) * std::cosf(theta * 2),
                    r * std::tanf(theta * 0.4f - tFactor * 0.3f) * std::sinf(theta * 1.5f));
            case 21: {
                float aurora = r * (0.7f + 0.1f * std::atanf(std::sinf(theta * 5 + tFactor * 2)));
                return Vector2D(aurora * std::cosf(theta + std::sinf(theta * 9 + tFactor)),
                    aurora * std::sinf(theta - std::cosf(theta * 8 - tFactor)));
            }
            case 22: {
                float bloom = r * (0.4f + 0.3f * std::powf(std::sinf(theta * 0.5f + tFactor * 0.4f), 5));
                return Vector2D(bloom * std::cosf(theta) * (1 + 0.5f * std::sinf(23 * theta + tFactor * 3)),
                    bloom * std::sinf(theta) * (1 + 0.5f * std::cosf(21 * theta - tFactor * 2)));
            }
            case 23:
                return Vector2D(r * std::asinf(std::sinf(theta + tFactor)) * std::cosf(3 * theta),
                    r * std::acosf(std::cosf(theta - tFactor)) * std::sinf(2 * theta));
            case 24: {
                float star = r * (0.5f + 0.3f * std::sinf(theta * 5 + tFactor * 1.5f));
                return Vector2D(star * std::cosf(theta) * (1 + 0.3f * std::sinf(8 * theta + tFactor * 2)),
                    star * std::sinf(theta) * (1 + 0.3f * std::cosf(8 * theta - tFactor * 2)));
            }
            case 25: {
                float a = r * 0.6f, b = r * 0.2f;
                return Vector2D((a + b) * std::cosf(theta) - b * std::cosf((a / b + 1) * theta + tFactor),
                    (a + b) * std::sinf(theta) - b * std::sinf((a / b + 1) * theta + tFactor));
            }
            case 26: {
                float c = r * 0.7f, d = r * 0.175f;
                return Vector2D((c - d) * std::cosf(theta) + d * std::cosf((c / d - 1) * theta - tFactor),
                    (c - d) * std::sinf(theta) - d * std::sinf((c / d - 1) * theta - tFactor));
            }
            case 27:
                return Vector2D(r * std::sinf(3 * theta + tFactor * 0.8f) * std::cosf(theta * param1),
                    r * std::sinf(4 * theta - tFactor * 0.9f) * std::sinf(theta * param2));
            case 28: {
                float R = r * 0.6f, r2 = r * 0.3f;
                return Vector2D((R + r2 * std::cosf(theta * 5 + tFactor)) * std::cosf(theta),
                    (R + r2 * std::sinf(theta * 5 + tFactor)) * std::sinf(theta));
            }
            case 29: {
                float cat = r * (std::coshf(theta * 0.5f + tFactor * 0.5f) - 1);
                return Vector2D(cat * std::cosf(theta + tFactor * 0.3f),
                    cat * std::sinf(theta - tFactor * 0.3f));
            }
            case 30:
                return Vector2D(r * (std::cosf(theta + tFactor) + theta * std::sinf(theta + tFactor)),
                    r * (std::sinf(theta + tFactor) - theta * std::cosf(theta + tFactor)));
            case 31: {
                float arch = r * (0.1f + 0.4f * (theta / TWO_PI + tFactor * 0.2f));
                return Vector2D(arch * std::cosf(theta), arch * std::sinf(theta));
            }
            case 32: {
                float card = r * (1 + std::cosf(theta + tFactor * 0.7f));
                return Vector2D(card * std::cosf(theta), card * std::sinf(theta));
            }
            case 33: {
                float lem = r * std::sqrtf(std::fabs(std::cosf(2 * theta + tFactor)));
                return Vector2D(lem * std::cosf(theta) * (1 + 0.2f * std::sinf(6 * theta + tFactor)),
                    lem * std::sinf(theta) * (1 + 0.2f * std::cosf(6 * theta - tFactor)));
            }
            case 34:
                return Vector2D(r * (2 * std::cosf(theta + tFactor) + std::cosf(2 * theta + tFactor * 2)),
                    r * (2 * std::sinf(theta + tFactor) - std::sinf(2 * theta + tFactor * 2)));
            case 35:
                return Vector2D(r * std::powf(std::cosf(theta + tFactor), 3),
                    r * std::powf(std::sinf(theta + tFactor), 3));
            case 36: {
                float k = r * 0.5f;
                return Vector2D(k * (3 * std::cosf(theta) - std::cosf(3 * theta + tFactor)),
                    k * (3 * std::sinf(theta) - std::sinf(3 * theta + tFactor)));
            }
            case 37: {
                float a1 = r * 0.7f, b1 = r * 0.5f;
                return Vector2D((a1 * a1 - b1 * b1) * std::cosf(theta + tFactor) * std::cosf(theta) / a1,
                    (a1 * a1 - b1 * b1) * std::sinf(theta + tFactor) * std::sinf(theta) / b1);
            }
            case 38: {
                float k1 = 0.1f + 0.05f * noise(tFactor);
                return Vector2D(r * std::expf(k1 * theta) * std::cosf(theta + tFactor * 0.5f),
                    r * std::expf(k1 * theta) * std::sinf(theta + tFactor * 0.5f));
            }
            case 39: {
                float s = theta * 0.5f + tFactor;
                return Vector2D(r * 0.5f * std::cosf(s * s) * (1 + 0.2f * std::sinf(5 * theta + tFactor)),
                    r * 0.5f * std::sinf(s * s) * (1 + 0.2f * std::cosf(5 * theta + tFactor)));
            }
            case 40: {
                float tr = r * (1 + 0.2f * noise(theta + tFactor));
                return Vector2D(tr * (std::cosf(theta) + std::logf(std::tanf(theta / 2 + tFactor * 0.1f))),
                    tr * std::sinf(theta));
            }
            case 41: {
                float cis = r * std::sinf(theta) * std::sinf(theta + tFactor);
                return Vector2D(cis * std::cosf(theta) / (1 - std::sinf(theta + tFactor)),
                    cis * std::sinf(theta) / (1 - std::sinf(theta + tFactor)));
            }
            case 42: {
                float n = std::floor(param1 + 0.5f);
                return Vector2D(r * std::cosf(n * theta + tFactor) * std::cosf(theta),
                    r * std::cosf(n * theta + tFactor) * std::sinf(theta));
            }
            case 43: {
                float theo = r * std::sqrtf(theta / M_PI + tFactor * 0.3f);
                return Vector2D(theo * std::cosf(theta), theo * std::sinf(theta));
            }
            case 44: {
                float a2 = r * 0.5f, b2 = r * 0.7f;
                return Vector2D(a2 * std::cosf(theta) + b2 * std::cosf(theta + tFactor) / std::cosf(theta),
                    a2 * std::sinf(theta) + b2 * std::sinf(theta + tFactor) / std::cosf(theta));
            }
            case 45: {
                float a3 = r * 0.5f;
                return Vector2D(a3 * (std::cosf(theta) - std::cosf(2 * theta + tFactor)) / std::sinf(theta),
                    a3 * (std::cosf(theta) + std::cosf(2 * theta + tFactor)) * std::sinf(theta));
            }
            case 46: {
                float a4 = r * 0.6f, b4 = r * 0.4f;
                return Vector2D((a4 + b4 * std::cosf(theta + tFactor)) * std::cosf(theta),
                    (a4 + b4 * std::cosf(theta + tFactor)) * std::sinf(theta));
            }
            case 47: {
                float a5 = r * 0.5f;
                return Vector2D(a5 * (1 / std::cosf(theta) + std::cosf(theta + tFactor)) * std::cosf(theta),
                    a5 * (1 / std::cosf(theta) + std::cosf(theta + tFactor)) * std::sinf(theta));
            }
            case 48: {
                float a6 = r * 0.7f;
                return Vector2D(a6 * std::cosf(theta) / (1 + std::sinf(theta + tFactor)),
                    a6 * std::cosf(theta) * std::sinf(theta + tFactor) / (1 + std::sinf(theta + tFactor)));
            }
            case 49: {
                float a7 = r * 0.6f, b7 = r * 0.3f;
                return Vector2D(a7 * (1 + std::sinf(theta + tFactor)) * std::cosf(theta),
                    b7 * (1 + std::sinf(theta + tFactor)) * std::sinf(theta));
            }
            case 50: {
                float a8 = r * 0.5f;
                return Vector2D(a8 * (std::powf(theta, 2) * std::cosf(theta + tFactor) - theta * std::sinf(theta)),
                    a8 * (std::powf(theta, 2) * std::sinf(theta + tFactor) + theta * std::cosf(theta)));
            }
            case 51: {
                float a9 = r * 0.5f, c9 = r * 0.7f;
                float rho = std::sqrtf(std::powf(a9 * std::cosf(2 * theta + tFactor), 2) + c9 * c9);
                return Vector2D(rho * std::cosf(theta), rho * std::sinf(theta));
            }
            case 52: {
                float a10 = r * 0.6f, b10 = r * 0.4f;
                float m = std::sqrtf(a10 * a10 - b10 * b10 * std::sinf(theta + tFactor) * std::sinf(theta + tFactor));
                return Vector2D(m * std::cosf(theta), b10 * std::sinf(theta + tFactor));
            }
            case 53: {
                float a11 = r * 0.5f;
                return Vector2D(a11 * theta * std::sinhf(theta + tFactor * 0.3f),
                    a11 * (std::coshf(theta + tFactor * 0.3f) - 1));
            }
            case 54: {
                float a12 = r * 0.6f;
                return Vector2D(a12 * std::sinf(theta + tFactor) * std::cosf(2 * theta + tFactor * 0.5f),
                    a12 * std::cosf(theta + tFactor) * std::sinf(2 * theta + tFactor * 0.5f));
            }
            case 55: {
                float a13 = r * 0.5f;
                return Vector2D(a13 * 3 * std::cosf(theta) / (1 + std::powf(std::sinf(theta + tFactor), 3)),
                    a13 * 3 * std::cosf(theta) * std::sinf(theta + tFactor) / (1 + std::powf(std::sinf(theta + tFactor), 3)));
            }
            case 56: {
                float a14 = r * 0.6f;
                return Vector2D(a14 * (2 * std::cosf(theta + tFactor) + 1) * std::cosf(theta),
                    a14 * (2 * std::cosf(theta + tFactor) + 1) * std::sinf(theta));
            }
            case 57: {
                float a15 = r * 0.5f;
                return Vector2D(a15 * std::cosf(theta) * (std::cosf(theta) - std::sinf(theta + tFactor)) / std::sinf(theta),
                    a15 * std::cosf(theta) * (std::cosf(theta) + std::sinf(theta + tFactor)));
            }
            case 58: {
                float a16 = r * 0.5f;
                return Vector2D(a16 * std::powf(theta, 2) * std::cosf(theta + tFactor),
                    a16 * std::powf(theta, 3) * std::sinf(theta + tFactor));
            }
            case 59: {
                float a17 = r * 0.5f;
                return Vector2D(a17 * (3 * std::cosf(theta) - std::cosf(3 * theta + tFactor)),
                    a17 * 3 * std::sinf(theta) * std::cosf(theta + tFactor) * std::cosf(theta + tFactor));
            }
            case 60: {
                float a18 = r * 0.6f, b18 = r * 0.3f;
                return Vector2D(a18 * std::cosf(theta) + b18 * std::sinf(theta + tFactor) / std::cosf(theta),
                    a18 * std::sinf(theta) + b18 * std::cosf(theta + tFactor) / std::cosf(theta));
            }
            case 61: {
                float a19 = r * 0.5f;
                return Vector2D(a19 * std::cosf(theta) / (1 + std::powf(std::sinf(theta + tFactor), 2)),
                    a19 * std::cosf(theta) * std::sinf(theta + tFactor) / (1 + std::powf(std::sinf(theta + tFactor), 2)));
            }
            case 62: {
                float a20 = r * 0.5f;
                return Vector2D(a20 * (theta - std::sinf(theta + tFactor)),
                    a20 * (1 - std::cosf(theta + tFactor)));
            }
            case 63: {
                float a21 = r * 0.6f, b21 = r * 0.4f;
                float rho2 = std::sqrtf((std::powf(a21 * std::sinf(theta + tFactor), 2) - std::powf(b21 * std::cosf(theta), 2)) / (1 - 0.5f * std::sinf(theta + tFactor) * std::sinf(theta + tFactor)));
                return Vector2D(rho2 * std::cosf(theta), rho2 * std::sinf(theta));
            }
            case 64: {
                float a22 = r * 0.5f, n22 = std::floor(param1 + 0.5f);
                return Vector2D(a22 * std::powf(std::fabs(std::cosf(theta + tFactor)), 1.0f / n22) * std::cosf(theta),
                    a22 * std::powf(std::fabs(std::sinf(theta + tFactor)), 1.0f / n22) * std::sinf(theta));
            }
            case 65: {
                float a23 = r * 0.5f;
                return Vector2D(a23 * 2 * std::cosf(theta) * (1 + 0.2f * std::sinf(5 * theta + tFactor)),
                    a23 * 2 / (1 + std::powf(std::tanf(theta + tFactor), 2)));
            }
            case 66: {
                float a24 = r * 0.5f;
                return Vector2D(a24 * (theta + std::sinhf(theta + tFactor) * std::cosf(theta)),
                    a24 * (std::coshf(theta + tFactor) - std::sinf(theta)));
            }
            case 67: {
                float a25 = r * 0.6f;
                return Vector2D(a25 * (3 * std::cosf(theta + tFactor) - 1) * std::cosf(theta),
                    a25 * (3 * std::cosf(theta + tFactor) - 1) * std::sinf(theta));
            }
            case 68: {
                float a26 = r * 0.5f;
                return Vector2D(a26 * std::sqrtf(1 / (theta + tFactor + 0.1f)) * std::cosf(theta),
                    a26 * std::sqrtf(1 / (theta + tFactor + 0.1f)) * std::sinf(theta));
            }
            case 69: {
                float a27 = r * 0.5f;
                return Vector2D(a27 * 2 * std::sinf(theta) * std::cosf(theta + tFactor) / (1 + std::powf(std::cosf(theta + tFactor), 2)),
                    a27 * 2 * std::sinf(theta) * std::sinf(theta + tFactor) / (1 + std::powf(std::cosf(theta + tFactor), 2)));
            }
            case 70: {
                float a28 = r * 0.5f;
                return Vector2D(a28 * std::cosf(theta) * (1 + 0.3f * noise(theta / 5 + tFactor)),
                    a28 / (1 + std::powf(theta + tFactor, 2)));
            }
            case 71: {
                float a29 = r * 0.6f, b29 = r * 0.2f, d29 = r * 0.3f;
                return Vector2D((a29 + b29) * std::cosf(theta) - d29 * std::cosf((a29 / b29 + 1) * theta + tFactor),
                    (a29 + b29) * std::sinf(theta) - d29 * std::sinf((a29 / b29 + 1) * theta + tFactor));
            }
            case 72: {
                float a30 = r * 0.7f, b30 = r * 0.2f, d30 = r * 0.25f;
                return Vector2D((a30 - b30) * std::cosf(theta) + d30 * std::cosf((a30 / b30 - 1) * theta - tFactor),
                    (a30 - b30) * std::sinf(theta) - d30 * std::sinf((a30 / b30 - 1) * theta - tFactor));
            }
            case 74: {
                float a32 = r * 0.5f;
                return Vector2D(a32 * std::sinf(theta + tFactor) / (1 + std::powf(std::cosf(theta), 2)),
                    a32 * std::sinf(theta) * std::cosf(theta + tFactor) / (1 + std::powf(std::cosf(theta), 2)));
            }
            case 75: {
                float a33 = r * 0.5f, b33 = r * 0.6f;
                return Vector2D(a33 * std::cosf(theta) + b33 * std::sinf(theta + tFactor) * std::cosf(theta),
                    a33 * std::sinf(theta) + b33 * std::sinf(theta + tFactor) * std::sinf(theta));
            }
            case 76: {
                float a34 = r * 0.5f;
                return Vector2D(a34 * (3 * std::cosf(theta) + std::cosf(3 * theta + tFactor)),
                    a34 * (3 * std::sinf(theta) + std::sinf(3 * theta + tFactor)));
            }
            case 77: {
                float a35 = r * 0.5f;
                return Vector2D(a35 * std::cosf(theta / 3 + tFactor) * std::cosf(theta) * std::cosf(theta),
                    a35 * std::cosf(theta / 3 + tFactor) * std::sinf(theta) * std::cosf(theta));
            }
            case 78: {
                float a36 = r * 0.5f;
                return Vector2D(a36 * (1 / (theta + tFactor + 0.1f)) * std::cosf(theta),
                    a36 * (1 / (theta + tFactor + 0.1f)) * std::sinf(theta));
            }
            case 79: {
                float a37 = r * 0.5f;
                return Vector2D(a37 * std::cosf(theta) / std::cosf(theta + tFactor),
                    a37 * std::tanf(theta + tFactor) * std::sinf(theta));
            }
            case 80: {
                float a38 = r * 0.5f;
                return Vector2D(a38 * std::cosf(theta) * (1 + std::sinf(4 * theta + tFactor)),
                    a38 * std::sinf(theta) * (1 + std::sinf(4 * theta + tFactor)));
            }
            case 81: {
                float a39 = r * 0.6f, b39 = r * 0.3f;
                float m2 = std::sqrtf(a39 * a39 + b39 * b39 * std::cosf(theta + tFactor) * std::cosf(theta + tFactor));
                return Vector2D(m2 * std::cosf(theta), b39 * std::cosf(theta + tFactor));
            }
            case 82: {
                float a40 = r * 0.7f, b40 = r * 0.5f;
                return Vector2D(a40 * std::cosf(theta) * (1 + 0.2f * std::sinf(5 * theta + tFactor)),
                    b40 * std::sinf(theta) * (1 + 0.2f * std::cosf(5 * theta + tFactor)));
            }
            case 83: {
                float a41 = r * 0.5f;
                return Vector2D(a41 * std::sinf(theta) * (std::expf(std::cosf(theta + tFactor)) - 2 * std::cosf(4 * theta) - std::powf(std::sinf(theta / 12), 5)),
                    a41 * std::cosf(theta) * (std::expf(std::cosf(theta + tFactor)) - 2 * std::cosf(4 * theta) - std::powf(std::sinf(theta / 12), 5)));
            }
            case 84: {
                float a42 = r * 0.5f;
                return Vector2D(a42 * (2 * std::cosf(theta + tFactor) + std::cosf(2 * theta + tFactor)),
                    a42 * (2 * std::sinf(theta + tFactor) - std::sinf(2 * theta + tFactor)));
            }
            case 85: {
                float a43 = r * 0.5f;
                return Vector2D(a43 * std::sinf(theta + tFactor) / (theta + tFactor + 0.1f),
                    a43 * std::cosf(theta) / (theta + tFactor + 0.1f));
            }
            case 86: {
                float a44 = r * 0.5f;
                return Vector2D(a44 * (theta / M_PI) * std::sinf(theta + tFactor),
                    a44 * std::cosf(theta) / (theta / M_PI + tFactor + 0.1f));
            }
            case 87: {
                float a45 = r * 0.5f, n45 = std::floor(param2 + 0.5f);
                return Vector2D(a45 * std::powf(std::fabs(std::cosf(theta + tFactor)), 2.0f / n45) * std::cosf(theta),
                    a45 * std::powf(std::fabs(std::sinf(theta + tFactor)), 2.0f / n45) * std::sinf(theta));
            }
            case 88:
                return Vector2D(r * std::sinf(5 * theta + tFactor * 0.7f) * std::cosf(theta * param1),
                    r * std::sinf(6 * theta - tFactor * 0.8f) * std::sinf(theta * param2));
            case 89: {
                float a46 = r * 0.5f, n46 = std::floor(param1 + 0.5f);
                float k46 = n46 * theta + tFactor;
                return Vector2D(a46 * std::sinf(n46 * k46) * std::cosf(k46),
                    a46 * std::sinf(n46 * k46) * std::sinf(k46));
            }
            case 90: {
                float s2 = theta * 0.4f + tFactor;
                return Vector2D(r * 0.4f * std::cosf(s2 * s2 + tFactor) * (1 + 0.3f * std::sinf(6 * theta)),
                    r * 0.4f * std::sinf(s2 * s2 + tFactor) * (1 + 0.3f * std::cosf(6 * theta)));
            }
            case 91: {
                float a47 = r * 0.5f;
                return Vector2D(a47 * (theta - std::sinf(theta + tFactor) + 0.2f * std::sinf(5 * theta)),
                    a47 * (1 - std::cosf(theta + tFactor) + 0.2f * std::cosf(5 * theta)));
            }
            case 92: {
                float a48 = r * 0.5f;
                return Vector2D(a48 * (1 + std::sinf(theta + tFactor)) * std::cosf(theta),
                    a48 * (1 + std::sinf(theta + tFactor)) * std::sinf(theta));
            }
            case 93: {
                float a49 = r * 0.5f;
                return Vector2D(a49 * std::powf(std::cosf(theta + tFactor), 5) * std::cosf(theta),
                    a49 * std::powf(std::sinf(theta + tFactor), 5) * std::sinf(theta));
            }
            case 94: {
                float a50 = r * 0.5f;
                return Vector2D(a50 * (2 * std::cosf(theta + tFactor) - std::cosf(2 * theta + tFactor * 1.5f)),
                    a50 * (2 * std::sinf(theta + tFactor) + std::sinf(2 * theta + tFactor * 1.5f)));
            }
            case 95: {
                float a51 = r * 0.5f;
                return Vector2D(a51 * (3 * std::cosf(theta) - std::cosf(5 * theta + tFactor)),
                    a51 * (3 * std::sinf(theta) - std::sinf(5 * theta + tFactor)));
            }
            case 96: {
                float a52 = r * 0.6f, b52 = r * 0.4f;
                return Vector2D(a52 * (1 + std::cosf(theta + tFactor)) * std::cosf(theta),
                    b52 * (1 + std::cosf(theta + tFactor)) * std::sinf(theta));
            }
            case 97: {
                float a53 = r * 0.5f;
                return Vector2D(a53 * 3 * std::sinf(theta) * std::cosf(theta + tFactor) / (1 + std::sinf(theta + tFactor)),
                    a53 * 3 * std::sinf(theta) * std::sinf(theta + tFactor) / (1 + std::sinf(theta + tFactor)));
            }
            case 98: {
                float a54 = r * 0.5f;
                return Vector2D(a54 * (std::cosf(theta) + std::logf(std::tanf(theta / 2 + tFactor * 0.2f))),
                    a54 * std::sinf(theta) * (1 + 0.2f * std::sinf(5 * theta + tFactor)));
            }
            case 99: {
                float a55 = r * 0.5f, b55 = r * 0.6f;
                return Vector2D(a55 * std::cosf(theta) + b55 * std::sinf(theta + tFactor) / std::cosf(theta + tFactor),
                    a55 * std::sinf(theta) + b55 * std::sinf(theta + tFactor) * std::sinf(theta));
            }

            default:
               // return Vector2D(r * std::cosf(theta), r * std::sinf(theta));
                return Vector2D(p3.x, p3.y);
            }
        }
        catch (...) {
            return Vector2D(r * std::cosf(theta), r * std::sinf(theta));
        }
    }


   
    void drawCurveType(SDL_Texture* texture, int type, float weight) {
        int resolution = PAR;
        bool hasRealAudio = !engine.isSimulationMode();
         

        float audioSizeBoost = hasRealAudio ?
            (1.0f + audioParams.smoothedBass * 0.7f + audioParams.beatIntensity * 0.6f) : 1.0f;

        float size = ((std::max)(width, height) * 0.5f + minRadius) * baseScale * audioSizeBoost;

        auto& palette = colorPalettes;
        std::vector<SDL_Point> points;
        points.reserve(resolution + 1);

        float rotationSpeed = hasRealAudio ?
            (audioParams.rotationSpeed * (1.0f + audioParams.beatIntensity * 0.8f)) : 1.0f;

        float tSlow = t * TIME_SLOWDOWN;
        float rotation = tSlow * 0.1f * ((type % 2) ? 0.5 : -0.5) * rotationSpeed;
        float sinWaveMod = std::sinf(tSlow * 0.5f + type) * 0.3f + 1.0f;

        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_SetRenderTarget(renderer, texture);
        SDL_SetRenderDrawColor(renderer, 244, 0, 0, 0);
        SDL_RenderClear(renderer);

        

        for (int i = 0; i <= resolution; i++) {
            float theta = i * (TWO_PI * 4) / resolution;

            float audioMod = hasRealAudio ?
                (1.0f + audioParams.smoothedMid * 0.3f * sinWaveMod) : 1.0f;

            Vector2D point = calculateCurvePoint(theta, size * audioMod, type);

            float rPoint = std::sqrtf(point.x * point.x + point.y * point.y);
            if (rPoint < minRadius * baseScale) {
                float scaleFactor = (minRadius * baseScale) / (rPoint + 0.01f);
                point.x *= scaleFactor;
                point.y *= scaleFactor;
            }

            float audioColorShift = hasRealAudio ?
                (audioParams.smoothedTreble * 30.0f + audioParams.beatIntensity * 50.0f) : 0.0f;

            float hue = std::fmodf(palette[i % 3] + audioColorShift, 360);
            float saturation = 85.0f + (hasRealAudio ? audioParams.smoothedAmplitude * 10.0f : 0.0f);
            float brightness = 95.0f + (hasRealAudio ? audioParams.beatIntensity * 10.0f : 0.0f);
            float alpha = weight * 0.6f * (hasRealAudio ? audioParams.globalAmplification : 1.0f);
             alpha = 255;

            float x_rot = point.x * std::cosf(rotation) - point.y * std::sinf(rotation);
            float y_rot = point.x * std::sinf(rotation) + point.y * std::cosf(rotation);
            points.push_back({ static_cast<int>(x_rot + width / 2), static_cast<int>(y_rot + height / 2) });

            if (i % 40 == 0) {
                SDL_Color pointColor = hsbToRgb(hue, saturation, brightness, alpha);
                filledCircleRGBA(renderer, points.back().x, points.back().y,
                    2, pointColor.r, pointColor.g, pointColor.b, pointColor.a);
            }
        }

        if (!points.empty()) {
            float hue = std::fmodf(palette[0] + t * 10.0f, 360.0f);
            SDL_Color lineColor = hsbToRgb(hue, 85.0f, 100.0f, 0.6f * weight);
            SDL_SetRenderDrawColor(renderer, lineColor.r, lineColor.g, lineColor.b, lineColor.a);
            SDL_RenderDrawLines(renderer, points.data(), static_cast<int>(points.size()));

            hue = std::fmodf(palette[1] + t * 5.0f, 360.0f);
            SDL_Color glowColor = hsbToRgb(hue, 70.0f, 100.0f, 0.15f * weight);
            SDL_SetRenderDrawColor(renderer, glowColor.r, glowColor.g, glowColor.b, glowColor.a);
            for (int i = -1; i <= 1; i += 2) {
                std::vector<SDL_Point> shifted = points;
                for (auto& p : shifted) { p.x += i; }
                SDL_RenderDrawLines(renderer, shifted.data(), static_cast<int>(shifted.size()));
            }
        }

        SDL_SetRenderTarget(renderer, nullptr);
    }


     

    void run() {
        SDL_Event e;
        Uint32 lastTime = SDL_GetTicks();

        while (running) {
            Uint32 currentTime = SDL_GetTicks();
            float deltaTime = (currentTime - lastTime) / 1000.0f;
            lastTime = currentTime;
            t += deltaTime; // Update time for curves

            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) {
                    running = false;
                }
                else if (e.type == SDL_KEYDOWN) {
                    if (e.key.keysym.sym == SDLK_ESCAPE) {
                        running = false;
                    }
                    else if (e.key.keysym.sym == SDLK_SPACE) {
                        printDebugInfo();
                    }
                    else if (e.key.keysym.sym == SDLK_c) {
                        currentCurve = (currentCurve + 1) % 6; // Cycle through curve types
                    }
                }
            }

            engine.update();
            updateVisualization(deltaTime);
            render();

            SDL_Delay(16);
        }
    }

private:
    void updateVisualization(float deltaTime) {
        wavePhase += deltaTime * 3.0f;

        std::vector<float> freqData = engine.getFrequencyData();
        float amplitude = engine.getAmplitude();
        float beat = engine.getBeat();
        float audioLevel = engine.getAudioLevel();

        // Update audio parameters for curves
        audioParams.smoothedAmplitude = amplitude;
        audioParams.beatDetected = beat > 0.1f;
        audioParams.beatIntensity = beat;
        audioParams.rotationSpeed = 1.0f + audioLevel * 2.0f;
        audioParams.globalAmplification = 1.0f + audioLevel * 0.5f;

        // Approximate frequency bands
        audioParams.smoothedBass = 0.0f;
        audioParams.smoothedMid = 0.0f;
        audioParams.smoothedTreble = 0.0f;
        if (!freqData.empty()) {
            int bassBins = freqData.size() / 4;
            int midBins = freqData.size() / 2;
            for (int i = 0; i < freqData.size(); ++i) {
                if (i < bassBins) audioParams.smoothedBass += freqData[i];
                else if (i < midBins) audioParams.smoothedMid += freqData[i];
                else audioParams.smoothedTreble += freqData[i];
            }
            audioParams.smoothedBass /= bassBins;
            audioParams.smoothedMid /= (midBins - bassBins);
            audioParams.smoothedTreble /= (freqData.size() - midBins);
        }

        float targetBg = amplitude * 100.0f + beat * 50.0f;
        backgroundIntensity = backgroundIntensity * 0.9f + targetBg * 0.1f;

        if (!freqData.empty()) {
            for (int i = 0; i < NUM_BARS; i++) {
                int freqIndex = (i * freqData.size()) / NUM_BARS;
                freqIndex = (std::min)(freqIndex, static_cast<int>(freqData.size()) - 1);

                targetHeights[i] = freqData[freqIndex] * SCREEN_HEIGHT * 0.8f;

                if (i < 4) {
                    targetHeights[i] += beat * SCREEN_HEIGHT * 0.2f;
                }

                float diff = targetHeights[i] - barHeights[i];
                barHeights[i] += diff * deltaTime * 8.0f;

                if (barHeights[i] < 0) barHeights[i] = 0;
            }
        }

        for (auto& wave : backgroundWaves) {
            wave.update(deltaTime);
        }

        if (audioLevel > 0.05f && particles.size() < PAR) {
            Particle::PaletteType paletteType = static_cast<Particle::PaletteType>(rng() % palettes.size());
            spawnParticles(particles, audioLevel, paletteType);
        }

        updateParticles(particles, audioLevel, beat, deltaTime);
    }

    void spawnParticles(std::vector<Particle>& particles, float audioLevel, Particle::PaletteType paletteType) {
        int count = static_cast<int>(audioLevel * 8.0f) + 1;

        const auto& palette = palettes[static_cast<size_t>(paletteType)];
        for (int i = 0; i < count; i++) {
            float x = static_cast<float>(rng() % SCREEN_WIDTH);
            float y = static_cast<float>(rng() % SCREEN_HEIGHT);

            SDL_Color color = palette[static_cast<size_t>(rng()) % palette.size()];

            Particle::Shape shape;
            int shapeRand = rng() % 6;
            if (shapeRand == 0) shape = Particle::Shape::Circle;
            else if (shapeRand == 1) shape = Particle::Shape::Rectangle;
            else if (shapeRand == 2) shape = Particle::Shape::Star;
            else if (shapeRand == 3) shape = Particle::Shape::Triangle;
            else if (shapeRand == 4) shape = Particle::Shape::Pentagon;
            else shape = Particle::Shape::Hexagon;

            Particle p(x, y, color, shape, paletteType);
            p.frequency = 0.1f + (static_cast<float>(rng()) / rng.max()) * 0.3f;
            p.amplitude = 30.0f + audioLevel * 50.0f;
            p.maxLife = 0.8f + audioLevel * 2.0f;
            p.life = p.maxLife;
            p.rotationSpeed = (static_cast<float>(rng()) / rng.max() - 0.5f) * 3.0f;
            p.scaleSpeed = (static_cast<float>(rng()) / rng.max() - 0.5f) * 0.7f;
            p.gravity = 0.001f;
            p.vx = (static_cast<float>(rng()) / rng.max() - 0.5f) * 300.0f;
            p.vy = (static_cast<float>(rng()) / rng.max() - 0.5f) * 300.0f;

            particles.push_back(p);
        }
    }

    void updateParticles(std::vector<Particle>& particles, float audioLevel, float beat, float deltaTime) {
        for (size_t i = 0; i < particles.size(); ++i) {
            particles[i].update(audioLevel, beat, deltaTime, particles, i);
        }

        particles.erase(std::remove_if(particles.begin(), particles.end(),
            [](const Particle& p) { return !p.isAlive(); }),
            particles.end());
    }

    void drawBackgroundWaves(float deltaTime) {
        float time = static_cast<float>(SDL_GetTicks());
        float beat = engine.getBeat();

        float audioMod = 1.0f + engine.getAmplitude() * (4.0f + beat);

      

        for (const auto& wave : backgroundWaves) {
            // Use a gradient color based on time or position
            Uint8 r = static_cast<Uint8>(wave.color.r * (0.5f + 0.5f * sin(time * 0.001f)));
            Uint8 g = static_cast<Uint8>(wave.color.g * (0.5f + 0.5f * sin(time * 0.002f)));
            Uint8 b = static_cast<Uint8>(wave.color.b * (0.5f + 0.5f * sin(time * 0.003f)));
            Uint8 a = wave.color.a;

            // Set the render draw color
            SDL_SetRenderDrawColor(renderer, r, g, b, a);

            // Draw anti-aliased lines using SDL2_gfx
            for (int x = 0; x < SCREEN_WIDTH - 1; x += 1) { // Reduced step size for smoother lines
                float y1 = static_cast<float>(SCREEN_HEIGHT) / 2.0f + wave.getY(static_cast<float>(x), time, audioMod);
                float y2 = static_cast<float>(SCREEN_HEIGHT) / 2.0f + wave.getY(static_cast<float>(x + 1), time, audioMod);

                // Use aaline for anti-aliased lines
                aalineRGBA(renderer, x, static_cast<int>(y1), x + 1, static_cast<int>(y2), r, g, b, a);
            }
        }
    }

     

    void drawParticles(const std::vector<Particle>& particles) {
        for (const auto& particle : particles) {
            Uint32 color = (particle.color.a << 24) | (particle.color.r << 16) | (particle.color.g << 8) | particle.color.b;
            int x = static_cast<int>(particle.x);
            int y = static_cast<int>(particle.y);
            int size = static_cast<int>(particle.size * particle.scale);

            switch (particle.shape) {
            case Particle::Shape::Circle:
                filledCircleColor(renderer, x, y, size / 2, color);
                if (size > 6) {
                    circleColor(renderer, x, y, size, (particle.color.a / 4 << 24) | (particle.color.r << 16) | (particle.color.g << 8) | particle.color.b);
                }
                break;
            case Particle::Shape::Rectangle:
                boxColor(renderer, x - size / 2, y - size / 2, x + size / 2, y + size / 2, color);
                if (size > 6) {
                    rectangleColor(renderer, x - size, y - size, x + size, y + size, (particle.color.a / 4 << 24) | (particle.color.r << 16) | (particle.color.g << 8) | particle.color.b);
                }
                break;
            case Particle::Shape::Star: {
                Sint16 vx[4] = { x, x + size / 2, x, x - size / 2 };
                Sint16 vy[4] = { y - size / 2, y, y + size / 2, y };
                filledPolygonColor(renderer, vx, vy, 4, color);
                if (size > 6) {
                    polygonColor(renderer, vx, vy, 4, (particle.color.a / 4 << 24) | (particle.color.r << 16) | (particle.color.g << 8) | particle.color.b);
                }
                break;
            }
            case Particle::Shape::Triangle: {
                Sint16 vx[3] = { x, x + size / 2, x - size / 2 };
                Sint16 vy[3] = { y - size / 2, y + size / 2, y + size / 2 };
                filledPolygonColor(renderer, vx, vy, 3, color);
                if (size > 6) {
                    polygonColor(renderer, vx, vy, 3, (particle.color.a / 4 << 24) | (particle.color.r << 16) | (particle.color.g << 8) | particle.color.b);
                }
                break;
            }
            case Particle::Shape::Pentagon: {
                Sint16 vx[5], vy[5];
                for (int i = 0; i < 5; i++) {
                    float angle = particle.rotation + i * 2.0f * M_PI / 5.0f;
                    vx[i] = x + size / 2 * cos(angle);
                    vy[i] = y + size / 2 * sin(angle);
                }
                filledPolygonColor(renderer, vx, vy, 5, color);
                if (size > 6) {
                    polygonColor(renderer, vx, vy, 5, (particle.color.a / 4 << 24) | (particle.color.r << 16) | (particle.color.g << 8) | particle.color.b);
                }
                break;
            }
            case Particle::Shape::Hexagon: {
                Sint16 vx[6], vy[6];
                for (int i = 0; i < 6; i++) {
                    float angle = particle.rotation + i * 2.0f * M_PI / 6.0f;
                    vx[i] = x + size / 2 * cos(angle);
                    vy[i] = y + size / 2 * sin(angle);
                }
                filledPolygonColor(renderer, vx, vy, 6, color);
                if (size > 6) {
                    polygonColor(renderer, vx, vy, 6, (particle.color.a / 4 << 24) | (particle.color.r << 16) | (particle.color.g << 8) | particle.color.b);
                }
                break;
            }
            }
        }
    }

    void printDebugInfo() {
        std::vector<float> freqData = engine.getFrequencyData();
        float amplitude = engine.getAmplitude();
        float beat = engine.getBeat();
        float audioLevel = engine.getAudioLevel();

        std::cout << "\n=== DEBUG INFO ===" << std::endl;
        std::cout << "Audio Level: " << audioLevel << std::endl;
        std::cout << "Amplitude: " << amplitude << std::endl;
        std::cout << "Beat: " << beat << std::endl;
        std::cout << "Freq Data Size: " << freqData.size() << std::endl;
        std::cout << "Particle Count: " << particles.size() << std::endl;
        std::cout << "Current Curve Type: " << currentCurve << std::endl;

        if (!freqData.empty()) {
            std::cout << "First 8 frequency values: ";
            for (int i = 0; i < (std::min)(8, static_cast<int>(freqData.size())); i++) {
                std::cout << freqData[i] << " ";
            }
            std::cout << std::endl;
        }

        std::cout << "Background Intensity: " << backgroundIntensity << std::endl;
        std::cout << "Mode: " << (engine.isSimulationMode() ? "SIMULATION" : "LIVE") << std::endl;
        std::cout << "=================" << std::endl;
    }

     

    SDL_Color plasmaRgb2(float hue, float saturation, float brightness, int palette) {
        SDL_Color rgb;



        switch (palette) {
        case 0: // Ice and Frost
            if (hue < 120) {
                hue = fmodf(hue * 2.5f, 240.0f);
                saturation = 80.0f + 20.0f * sinf(hue * 0.01f);
                brightness = 70.0f + 30.0f * cosf(hue * 0.01f);
            }
            else if (hue < 240) {
                hue = fmodf((hue - 120.0f) * 2.0f, 120.0f);
                saturation = 90.0f + 10.0f * sinf(hue * 0.01f);
                brightness = 80.0f + 20.0f * cosf(hue * 0.01f);
            }
            else {
                hue = fmodf((hue - 240.0f) * 3.0f, 120.0f);
                saturation = 100.0f;
                brightness = 90.0f + 10.0f * sinf(hue * 0.01f);
            }
            break;

        case 1: // Deep Blue
            hue = fmodf(hue * 1.5f, 240.0f);
            saturation = 90.0f + 10.0f * sinf(hue * 0.01f);
            brightness = 40.0f + 20.0f * cosf(hue * 0.01f);
            break;

        case 2: // Smoke Volcano
            if (hue < 120) {
                hue = fmodf(hue * 2.0f, 120.0f);
                saturation = 60.0f + 20.0f * sinf(hue * 0.01f);
                brightness = 50.0f + 30.0f * cosf(hue * 0.01f);
            }
            else {
                hue = fmodf((hue - 120.0f) * 2.0f, 120.0f);
                saturation = 80.0f + 10.0f * sinf(hue * 0.01f);
                brightness = 70.0f + 20.0f * cosf(hue * 0.01f);
            }
            break;

        case 3: // Electric Storm
            hue = fmodf(hue * 2.0f, 360.0f);
            saturation = 100.0f;
            brightness = 80.0f + 20.0f * sinf(hue * 0.01f);
            break;

        case 4: // Toxic Waste
            hue = fmodf(hue * 1.5f, 120.0f);
            saturation = 80.0f + 20.0f * sinf(hue * 0.01f);
            brightness = 60.0f + 30.0f * cosf(hue * 0.01f);
            break;

        case 5: // Neon Dreams
            hue = fmodf(hue * 2.5f, 360.0f);
            saturation = 90.0f + 10.0f * sinf(hue * 0.01f);
            brightness = 90.0f + 10.0f * cosf(hue * 0.01f);
            break;
        }

        // Convert HSB to RGB
        float r, g, b;
        int hi = (int)(hue / 60.0f) % 6;
        float f = hue / 60.0f - hi;
        float p = brightness * (1.0f - saturation / 100.0f);
        float q = brightness * (1.0f - f * saturation / 100.0f);
        float t = brightness * (1.0f - (1.0f - f) * saturation / 100.0f);

        switch (hi) {
        case 0: r = brightness; g = t; b = p; break;
        case 1: r = q; g = brightness; b = p; break;
        case 2: r = p; g = brightness; b = t; break;
        case 3: r = p; g = q; b = brightness; break;
        case 4: r = t; g = p; b = brightness; break;
        case 5: r = brightness; g = p; b = q; break;
        }

        rgb.r = (Uint8)(r * 255.0f / 100.0f);
        rgb.g = (Uint8)(g * 255.0f / 100.0f);
        rgb.b = (Uint8)(b * 255.0f / 100.0f);
        rgb.a = 255;

        return rgb;
    }
    void plasma2(float audioBassLevel = 0.5f, float deltaTime = 0.016f) {
        static float t = 0.0f;
        static SDL_Texture* plasmaTex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
            SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH, SCREEN_HEIGHT);
        static int currentPaletteIndex = rand() % 6;
        static int nextPaletteIndex = rand() % 6;
        static float paletteTransitionTime = 0.2f;
        const float transitionDuration = 10.0f; // 20 seconds

        t += deltaTime;
        paletteTransitionTime += deltaTime;

        if (paletteTransitionTime >= transitionDuration) {
            currentPaletteIndex = nextPaletteIndex;
            nextPaletteIndex = rand() % 6;
            paletteTransitionTime = 0.1f;
        }

        float paletteInterpolation = paletteTransitionTime / transitionDuration;

        // Time-based parameters for animation
        float time1 = t * 0.3f;
        float time2 = t * 0.5f;
        float time3 = t * 0.7f;
        float colorCycle = t * 0.2f;

        // Modulate effects with audio
        float audioInfluence = 0.7f + 0.9f * audioBassLevel;
        float swirlFactor = 1.0f * audioInfluence;

        void* pixels;
        int pitch;
        SDL_LockTexture(plasmaTex, nullptr, &pixels, &pitch);
        Uint32* pixelBuffer = static_cast<Uint32*>(pixels);

        // Precompute screen aspect and center
        float aspect = SCREEN_WIDTH / (float)SCREEN_HEIGHT;
        float centerX = SCREEN_WIDTH / 2.0f;
        float centerY = SCREEN_HEIGHT / 2.0f;

        // Generate plasma pattern
        for (int y = 0; y < SCREEN_HEIGHT; ++y) {
            float ny = ((y - centerY) / centerY) * 1.0f;

            for (int x = 0; x < SCREEN_WIDTH; ++x) {
                float nx = ((x - centerX) / centerX) * aspect;

                // Swirling transformation
                float angle = atan2f(ny, nx);
                float radius = sqrtf(nx * nx + ny * ny);

                // Dynamic swirl modulated by time and audio
                angle += swirlFactor / (radius + 0.3f) + time1;
                radius = radius * (0.8f + 0.2f * sinf(time2 * 0.5f));

                float sx = radius * cosf(angle);
                float sy = radius * sinf(angle);

                // Multiple sine waves for complex interference
                float value =
                    0.50f * sinf(sx * 7.0f + time1) +
                    0.35f * sinf(sy * 9.0f + time2) +
                    0.25f * sinf((sx + sy) * 5.0f + time3) +
                    0.30f * sinf(radius * 15.0f + colorCycle);
                value = (value + 1.0f) * 0.5f;

                // Dynamic HSB color with audio-reactive brightness
                float hue = fmodf(colorCycle * 40.0f + value * 120.0f + radius * 60.0f, 360.0f);
                float saturation = 70.0f + 30.0f * sinf(time3);
                float brightness = 20.0f + 60.0f * value + 30.0f * audioBassLevel;

                SDL_Color currentColor = plasmaRgb(hue, saturation, brightness, currentPaletteIndex);
                SDL_Color nextColor = plasmaRgb(hue, saturation, brightness, nextPaletteIndex);

                SDL_Color interpolatedColor;
                interpolatedColor.r = (Uint8)((1.0f - paletteInterpolation) * currentColor.r + paletteInterpolation * nextColor.r);
                interpolatedColor.g = (Uint8)((1.0f - paletteInterpolation) * currentColor.g + paletteInterpolation * nextColor.g);
                interpolatedColor.b = (Uint8)((1.0f - paletteInterpolation) * currentColor.b + paletteInterpolation * nextColor.b);
                interpolatedColor.a = 255;

                // Write pixel (RGBA8888: assuming little-endian ABGR layout)
                pixelBuffer[y * (pitch / 4) + x] =
                    (0xFF << 24) | (interpolatedColor.b << 16) | (interpolatedColor.g << 8) | interpolatedColor.r;
            }
        }

        SDL_UnlockTexture(plasmaTex);

        // Render plasma background
        SDL_RenderCopy(renderer, plasmaTex, nullptr, nullptr);

        // Add vortex overlay with additive blending
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
        for (int i = 0; i < 3; ++i) {
            float vTime = t * (0.3f + i * 0.1f);
            float vRadius = SCREEN_WIDTH * (0.25f + 0.15f * sinf(vTime * 0.5f + i));

            for (int a = 0; a < 360; a += 6) {
                float angle = a * M_PI / 180.0f;
                float x = centerX + cosf(angle + vTime) * vRadius;
                float y = centerY + sinf(angle + vTime) * vRadius;

                SDL_Color currentColor = plasmaRgb(
                    fmodf(a * 3.0f + colorCycle * 50.0f, 360.0f),
                    90.0f,
                    70.0f + 20.0f * sinf(t + a * 0.1f),
                    currentPaletteIndex
                );

                SDL_Color nextColor = plasmaRgb(
                    fmodf(a * 3.0f + colorCycle * 50.0f, 360.0f),
                    90.0f,
                    70.0f + 20.0f * sinf(t + a * 0.1f),
                    nextPaletteIndex
                );

                SDL_Color interpolatedColor;
                interpolatedColor.r = (Uint8)((1.0f - paletteInterpolation) * currentColor.r + paletteInterpolation * nextColor.r);
                interpolatedColor.g = (Uint8)((1.0f - paletteInterpolation) * currentColor.g + paletteInterpolation * nextColor.g);
                interpolatedColor.b = (Uint8)((1.0f - paletteInterpolation) * currentColor.b + paletteInterpolation * nextColor.b);
                interpolatedColor.a = 100;

                SDL_SetRenderDrawColor(renderer, interpolatedColor.r, interpolatedColor.g, interpolatedColor.b, interpolatedColor.a);
                SDL_RenderDrawLine(renderer, (int)centerX, (int)centerY, (int)x, (int)y);
            }
        }
    }


 

    SDL_Color plasmaRgb(float hue, float saturation, float brightness, int palette) {
        SDL_Color rgb;

        switch (palette) {
        case 0: // Ice and Frost
            if (hue < 120) {
                hue = fmodf(hue * 2.5f, 240.0f);
                saturation = 80.0f + 20.0f * sinf(hue * 0.01f);
                brightness = 70.0f + 30.0f * cosf(hue * 0.01f);
            }
            else if (hue < 240) {
                hue = fmodf((hue - 120.0f) * 2.0f, 120.0f);
                saturation = 90.0f + 10.0f * sinf(hue * 0.01f);
                brightness = 80.0f + 20.0f * cosf(hue * 0.01f);
            }
            else {
                hue = fmodf((hue - 240.0f) * 3.0f, 120.0f);
                saturation = 100.0f;
                brightness = 90.0f + 10.0f * sinf(hue * 0.01f);
            }
            break;

        case 1: // Deep Blue
            hue = fmodf(hue * 1.5f, 240.0f);
            saturation = 90.0f + 10.0f * sinf(hue * 0.01f);
            brightness = 40.0f + 20.0f * cosf(hue * 0.01f);
            break;

        case 2: // Smoke Volcano
            if (hue < 120) {
                hue = fmodf(hue * 2.0f, 120.0f);
                saturation = 60.0f + 20.0f * sinf(hue * 0.01f);
                brightness = 50.0f + 30.0f * cosf(hue * 0.01f);
            }
            else {
                hue = fmodf((hue - 120.0f) * 2.0f, 120.0f);
                saturation = 80.0f + 10.0f * sinf(hue * 0.01f);
                brightness = 70.0f + 20.0f * cosf(hue * 0.01f);
            }
            break;

        case 3: // Electric Storm
            hue = fmodf(hue * 2.0f, 360.0f);
            saturation = 100.0f;
            brightness = 80.0f + 20.0f * sinf(hue * 0.01f);
            break;

        case 4: // Toxic Waste
            hue = fmodf(hue * 1.5f, 120.0f);
            saturation = 80.0f + 20.0f * sinf(hue * 0.01f);
            brightness = 60.0f + 30.0f * cosf(hue * 0.01f);
            break;

        case 5: // Neon Dreams
            hue = fmodf(hue * 2.5f, 360.0f);
            saturation = 90.0f + 10.0f * sinf(hue * 0.01f);
            brightness = 90.0f + 10.0f * cosf(hue * 0.01f);
            break;
        }

        // Convert HSB to RGB
        float r, g, b;
        int hi = (int)(hue / 60.0f) % 6;
        float f = hue / 60.0f - hi;
        float p = brightness * (1.0f - saturation / 100.0f);
        float q = brightness * (1.0f - f * saturation / 100.0f);
        float t = brightness * (1.0f - (1.0f - f) * saturation / 100.0f);

        switch (hi) {
        case 0: r = brightness; g = t; b = p; break;
        case 1: r = q; g = brightness; b = p; break;
        case 2: r = p; g = brightness; b = t; break;
        case 3: r = p; g = q; b = brightness; break;
        case 4: r = t; g = p; b = brightness; break;
        case 5: r = brightness; g = p; b = q; break;
        }

        rgb.r = (Uint8)(r * 255.0f / 100.0f);
        rgb.g = (Uint8)(g * 255.0f / 100.0f);
        rgb.b = (Uint8)(b * 255.0f / 100.0f);
        rgb.a = 255;

        return rgb;
    }

    void plasma(float audioBassLevel,   float deltaTime = 0.016f) {
        static float t = 0.0f;
        static SDL_Texture* plasmaTex = NULL;
        if (!plasmaTex) {
            plasmaTex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH, SCREEN_HEIGHT);
        }
        static int currentPaletteIndex = rand() % 6;
        static int nextPaletteIndex = rand() % 6;
        static float paletteTransitionTime = 0.2f;
        const float transitionDuration = 10.0f; // 20 seconds

        t += deltaTime;
        paletteTransitionTime += deltaTime;

        if (paletteTransitionTime >= transitionDuration) {
            currentPaletteIndex = nextPaletteIndex;
            nextPaletteIndex = rand() % 6;
            paletteTransitionTime = 0.1f;
        }

        float paletteInterpolation = paletteTransitionTime / transitionDuration;

        // Random seed for shape variation
        static float shapeSeed = (float)rand() / RAND_MAX;
        if (paletteTransitionTime < 0.2f) {
            shapeSeed = (float)rand() / RAND_MAX; // New random seed per palette switch
        }

        // Time-based parameters with random variation
        float time1 = t * (0.3f + 0.1f * shapeSeed);
        float time2 = t * (0.5f + 0.2f * shapeSeed);
        float time3 = t * (0.7f + 0.15f * shapeSeed);
        float colorCycle = t * (0.2f + 0.1f * shapeSeed);

        // Modulate effects with audio
        float audioInfluence = 0.7f + 0.9f * audioBassLevel;
        float swirlFactor = 1.0f * audioInfluence;

        void* pixels;
        int pitch;
        SDL_LockTexture(plasmaTex, nullptr, &pixels, &pitch);
        Uint32* pixelBuffer = static_cast<Uint32*>(pixels);

        // Precompute screen aspect and center
        float aspect = SCREEN_WIDTH / (float)SCREEN_HEIGHT;
        float centerX = SCREEN_WIDTH / 2.0f;
        float centerY = SCREEN_HEIGHT / 2.0f;

        // Generate plasma pattern with random shape variations
        for (int y = 0; y < SCREEN_HEIGHT; ++y) {
            float ny = ((y - centerY) / centerY) * (1.0f + 0.2f * shapeSeed);

            for (int x = 0; x < SCREEN_WIDTH; ++x) {
                float nx = ((x - centerX) / centerX) * aspect;

                // Swirling transformation with random perturbation
                float angle = atan2f(ny, nx) + 0.5f * sinf(t * 0.1f + shapeSeed);
                float radius = sqrtf(nx * nx + ny * ny);

                // Dynamic swirl modulated by time, audio, and random seed
                angle += swirlFactor / (radius + 0.3f + 0.2f * shapeSeed) + time1;
                radius = radius * (0.8f + 0.3f * sinf(time2 * (0.5f + shapeSeed)));

                float sx = radius * cosf(angle);
                float sy = radius * sinf(angle);

                // Multiple sine waves with random coefficients for varied patterns
                float value =
                    0.50f * sinf(sx * (7.0f + 3.0f * shapeSeed) + time1) +
                    0.35f * sinf(sy * (9.0f + 4.0f * shapeSeed) + time2) +
                    0.25f * sinf((sx + sy) * (5.0f + 2.0f * shapeSeed) + time3) +
                    0.30f * sinf(radius * (15.0f + 5.0f * shapeSeed) + colorCycle);
                value = (value + 1.0f) * 0.5f;

                // Dynamic HSB color with audio-reactive brightness
                float hue = fmodf(colorCycle * 40.0f + value * 120.0f + radius * (60.0f + 20.0f * shapeSeed), 360.0f);
                float saturation = 70.0f + 30.0f * sinf(time3 + shapeSeed);
                float brightness = 20.0f + 60.0f * value + 30.0f * audioBassLevel;

                SDL_Color currentColor = plasmaRgb(hue, saturation, brightness, currentPaletteIndex);
                SDL_Color nextColor = plasmaRgb(hue, saturation, brightness, nextPaletteIndex);

                SDL_Color interpolatedColor;
                interpolatedColor.r = (Uint8)((1.0f - paletteInterpolation) * currentColor.r + paletteInterpolation * nextColor.r);
                interpolatedColor.g = (Uint8)((1.0f - paletteInterpolation) * currentColor.g + paletteInterpolation * nextColor.g);
                interpolatedColor.b = (Uint8)((1.0f - paletteInterpolation) * currentColor.b + paletteInterpolation * nextColor.b);
                interpolatedColor.a = 255;

                // Write pixel (RGBA8888: assuming little-endian ABGR layout)
                pixelBuffer[y * (pitch / 4) + x] =
                    (0xFF << 24) | (interpolatedColor.b << 16) | (interpolatedColor.g << 8) | interpolatedColor.r;
            }
        }

        SDL_UnlockTexture(plasmaTex);

        // Render plasma background
        SDL_RenderCopy(renderer, plasmaTex, nullptr, nullptr);

        // Add vortex overlay with additive blending and random shape variations
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
        for (int i = 0; i < 3; ++i) {
            float vTime = t * (0.3f + i * (0.1f + 0.05f * shapeSeed));
            float vRadius = SCREEN_WIDTH * (0.25f + 0.15f * sinf(vTime * (0.5f + shapeSeed) + i));

            // Randomize number of segments for varied shapes
            int segments = 360 / (6 + (int)(3.0f * shapeSeed));
            for (int a = 0; a < 360; a += segments) {
                float angle = a * M_PI / 180.0f + 0.2f * sinf(t * 0.2f + shapeSeed);
                float x = centerX + cosf(angle + vTime) * vRadius * (1.0f + 0.3f * shapeSeed);
                float y = centerY + sinf(angle + vTime) * vRadius * (1.0f + 0.3f * shapeSeed);

                SDL_Color currentColor = plasmaRgb(
                    fmodf(a * (3.0f + shapeSeed) + colorCycle * 50.0f, 360.0f),
                    90.0f,
                    70.0f + 20.0f * sinf(t + a * (0.1f + 0.05f * shapeSeed)),
                    currentPaletteIndex
                );

                SDL_Color nextColor = plasmaRgb(
                    fmodf(a * (3.0f + shapeSeed) + colorCycle * 50.0f, 360.0f),
                    90.0f,
                    70.0f + 20.0f * sinf(t + a * (0.1f + 0.05f * shapeSeed)),
                    nextPaletteIndex
                );

                SDL_Color interpolatedColor;
                interpolatedColor.r = (Uint8)((1.0f - paletteInterpolation) * currentColor.r + paletteInterpolation * nextColor.r);
                interpolatedColor.g = (Uint8)((1.0f - paletteInterpolation) * currentColor.g + paletteInterpolation * nextColor.g);
                interpolatedColor.b = (Uint8)((1.0f - paletteInterpolation) * currentColor.b + paletteInterpolation * nextColor.b);
                interpolatedColor.a = 100;

                SDL_SetRenderDrawColor(renderer, interpolatedColor.r, interpolatedColor.g, interpolatedColor.b, interpolatedColor.a);
                SDL_RenderDrawLine(renderer, (int)centerX, (int)centerY, (int)x, (int)y);
            }
        }
    }

    void render() {

        float beat = engine.getBeat();

        plasma(beat);



      

        // Draw particles
        drawParticles(particles);

        // Draw spectrum bars with increased transparency
        int barWidth = SCREEN_WIDTH / NUM_BARS;

        barWidth = SCREEN_WIDTH / NUM_BARS;


        /////////////////////////////////////////////////////////////
        /*
        for (int i = 0; i < NUM_BARS; i++) {
            int x = i * barWidth;
            int barHeight = static_cast<int>(barHeights[i]);

            if (barHeight > 2) {
                float colorPhase = static_cast<float>(i) / NUM_BARS;
                Uint8 red = static_cast<Uint8>(255 * (1.0f - colorPhase) + backgroundIntensity * 0.3f);
                Uint8 green = static_cast<Uint8>(128 + backgroundIntensity * 0.2f);
                Uint8 blue = static_cast<Uint8>(255 * colorPhase + backgroundIntensity * 0.3f);

                SDL_SetRenderDrawColor(renderer, red, green, blue, 220);  
                SDL_Rect barRect = {
                    x + 2,
                    SCREEN_HEIGHT - barHeight,
                    barWidth - 4,
                    barHeight
                };
                SDL_RenderFillRect(renderer, &barRect);

                SDL_SetRenderDrawColor(renderer, 255, 0, 255, 220); // Reduced alpha from 100 to 60
                SDL_Rect highlightRect = {
                    x + 2,
                    SCREEN_HEIGHT - barHeight,
                    barWidth - 4,
                    std::max(2, barHeight / 10)
                };
                SDL_RenderFillRect(renderer, &highlightRect);

                SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255); // Reduced alpha from 80 to 40
                SDL_RenderDrawRect(renderer, &barRect);
            }
        }
        */

        /////////////////////////////////////////////////////////////
        for (int i = 0; i < NUM_BARS; i++) {
            int x = i * barWidth;
            int barHeight = static_cast<int>(barHeights[i]);

            if (barHeight > 2) {
                static float t = 0.0f;
                static int currentPaletteIndex = rand() % 6;
                static int nextPaletteIndex = rand() % 6;
                static float paletteTransitionTime = 0.2f;
                const float transitionDuration = 10.0f;

                t += 0.016f; // Assuming deltaTime of 0.016f (60 FPS)
                paletteTransitionTime += 0.0001f;

                if (paletteTransitionTime >= transitionDuration) {
                    currentPaletteIndex = nextPaletteIndex;
                    //nextPaletteIndex = rand() % 2 ? 0 : 5;
                    nextPaletteIndex = rand() % 6;
                    paletteTransitionTime = 3.0f;
                }

                float paletteInterpolation = paletteTransitionTime / transitionDuration;
                float colorCycle = t * 0.05f;
                float hue = fmodf(colorCycle * 40.0f + (float)i / NUM_BARS * 120.0f, 360.0f);
                float saturation = 70.0f + 30.0f * sinf(t * 0.7f);
                float brightness = 20.0f + 60.0f * (barHeight / (float)SCREEN_HEIGHT) + 30.0f * engine.getBeat();

                SDL_Color currentColor = plasmaRgb(hue, saturation, brightness, currentPaletteIndex);
                SDL_Color nextColor = plasmaRgb(hue, saturation, brightness, nextPaletteIndex);

                SDL_Color interpolatedColor;
                interpolatedColor.r = (Uint8)((1.0f - paletteInterpolation) * currentColor.r + paletteInterpolation * nextColor.r);
                interpolatedColor.g = (Uint8)((1.0f - paletteInterpolation) * currentColor.g + paletteInterpolation * nextColor.g);
                interpolatedColor.b = (Uint8)((1.0f - paletteInterpolation) * currentColor.b + paletteInterpolation * nextColor.b);
                interpolatedColor.a = 220;

                SDL_SetRenderDrawColor(renderer, interpolatedColor.r, interpolatedColor.g, interpolatedColor.b, interpolatedColor.a);
                SDL_Rect barRect = {
                    x + 2,
                    SCREEN_HEIGHT - barHeight,
                    barWidth - 4,
                    barHeight
                };
                SDL_RenderFillRect(renderer, &barRect);

                SDL_Color highlightColor = plasmaRgb(hue + 30.0f, saturation, brightness * 0.8f, currentPaletteIndex);
                interpolatedColor.r = (Uint8)((1.0f - paletteInterpolation) * highlightColor.r + paletteInterpolation * plasmaRgb(hue + 30.0f, saturation, brightness * 0.8f, nextPaletteIndex).r);
                interpolatedColor.g = (Uint8)((1.0f - paletteInterpolation) * highlightColor.g + paletteInterpolation * plasmaRgb(hue + 30.0f, saturation, brightness * 0.8f, nextPaletteIndex).g);
                interpolatedColor.b = (Uint8)((1.0f - paletteInterpolation) * highlightColor.b + paletteInterpolation * plasmaRgb(hue + 30.0f, saturation, brightness * 0.8f, nextPaletteIndex).b);
                interpolatedColor.a = 220;

                SDL_SetRenderDrawColor(renderer, interpolatedColor.r, interpolatedColor.g, interpolatedColor.b, interpolatedColor.a);
                SDL_Rect highlightRect = {
                    x + 2,
                    SCREEN_HEIGHT - barHeight,
                    barWidth - 4,
                    std::max(2, barHeight / 10)
                };
                SDL_RenderFillRect(renderer, &highlightRect);

                SDL_SetRenderDrawColor(renderer, 255, 0, 255, 220); // Reduced alpha from 100 to 60
                  highlightRect = {
                    x + 2,
                    SCREEN_HEIGHT - barHeight,
                    barWidth - 4,
                    std::max(2, barHeight / 10)
                };
                SDL_RenderFillRect(renderer, &highlightRect);

                SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255); // Reduced alpha from 80 to 40
                SDL_RenderDrawRect(renderer, &barRect);
            }
        }

        ///////////////////////////////////////////////////////////////

        // Draw background waves
        drawBackgroundWaves(1.0f / 60.0f);

        // Draw curves
            drawCurveType(curveTexture, currentCurve, 0.9f);
        SDL_RenderCopy(renderer, curveTexture, nullptr, nullptr);

       
       


        //audio level
         //center line
        int centerY = SCREEN_HEIGHT / 2 + static_cast<int>(beat * 50.0f * std::sin(wavePhase));
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, static_cast<Uint8>(100 + beat * 155));
        SDL_RenderDrawLine(renderer, 0, centerY, SCREEN_WIDTH, centerY);

        /*
        // Draw audio level indicator  
        float audioLevel = engine.getAudioLevel();
        if (audioLevel > 0.01f) {
            SDL_SetRenderDrawColor(renderer, 0, 255, 0, 200);
            SDL_Rect levelRect = {
                10, 10,
                static_cast<int>(audioLevel * 200.0f), 20
            };
            SDL_RenderFillRect(renderer, &levelRect);

            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 150);
            SDL_Rect textBg = { 5, 5, 210, 30 };
            SDL_RenderFillRect(renderer, &textBg);
        }
        */
         
        // Status indicator
      //  SDL_SetRenderDrawColor(renderer,
       //     engine.isSimulationMode() ? 255 : 0,
       //     engine.isSimulationMode() ? 100 : 255,
       //     100, 200);
       // SDL_Rect statusRect = { SCREEN_WIDTH - 120, 10, 100, 20 };
       // SDL_RenderFillRect(renderer, &statusRect);

        SDL_RenderPresent(renderer);
    }



    void cleanup() {
        engine.cleanup();

        if (curveTexture) {
            SDL_DestroyTexture(curveTexture);
            curveTexture = nullptr;
        }
        if (renderer) {
            SDL_DestroyRenderer(renderer);
            renderer = nullptr;
        }
        if (window) {
            SDL_DestroyWindow(window);
            window = nullptr;
        }

        SDL_Quit();
    }
};

int main(int argc, char* args[]) {
    SimpleVisualizer viz;

    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        std::cerr << "Main CoInitializeEx failed: " << _com_error(hr).ErrorMessage() << std::endl;
        return 1;
    }

    if (!viz.initialize()) {
        std::cerr << "Failed to initialize visualizer!" << std::endl;
        return -1;
    }

    viz.run();
    return 0;
}