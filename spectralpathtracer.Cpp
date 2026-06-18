#define SDL_MAIN_USE_CALLBACKS 1
#define SDL_MOUSE_TOUCHID ((SDL_TouchID)-1)
constexpr float M_PI = 3.141592653589793238462643383279502884F;
constexpr float M_PI2 = 6.283185307179586476925286766559005768F;
#include <cpuid.h>
#include <immintrin.h>
#include <stdint.h>
#include <stdlib.h>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stddef.h>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <array>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <omp.h>
#include <string>
#include <inttypes.h>
#define width 480
#define height 360
#define screenWidth 480
#define screenHeight 360
//#define debug
#pragma GCC optimize("unroll-loops")
constexpr uint_fast32_t halfw = width>>1;
constexpr uint_fast32_t halfh = height>>1;
const uint_fast32_t screensize = width * height;
static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *framebuffer = NULL;
// Add mutex for thread synchronization
static SDL_Mutex *render_mutex = NULL;
constexpr float minreq = 0.001f;
constexpr float half_M_PI = M_PI*0.5f;
struct CPUFeatures {
    bool hasSSE41 = false;
    bool hasAVX = false;
};

CPUFeatures check_cpu() {
    CPUFeatures f;
    unsigned int eax, ebx, ecx, edx;

    // Level 1: SSE, AVX, etc.
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        f.hasSSE41 = (ecx & (1 << 19));
        f.hasAVX   = (ecx & (1 << 28));
    }
    return f;
}

// Global state set at startup
static CPUFeatures cpu;

typedef struct alignas(16){
    float x, y, z;
} vec3;
#define max_obj_type 16
constexpr uint16_t empty = 1 << 0, cube = 1 << 1, checkered = 1 << 2, light = 1 << 3, pointlight = 1 << 4, spotlight = 1 << 5, fog = 1 << 6, portal = 1 << 7, refract = 1 << 8, sphere = 1 << 9, cylinder = 1 << 10,bend = 1 << 11,prism = 1 << 12;
#define object_fields 14
constexpr uint_fast8_t bins = 12u;
typedef struct object {
    vec3 position;
    vec3 rotation;
    vec3 scale;
	vec3 position2;
    float color[bins] = {};
	float rayleigh[bins] = {};
	float rainbow;
    uint16_t type;
	float roughness;
	float specular;
	float specularroughness;
	float thickness;
	float anisotropy;
	float ior;
	float refractpassness;
} object;
std::vector<object>objects = {};
std::vector<object>lights = {};
float pm_render_ms = 0.0f;
uint8_t *pixels = nullptr;
SDL_Thread *thread;
bool running = true;
int renderthreadreturnvalue;
uint_fast16_t bounces = 0;
uint_fast16_t samples = 0;
int iterations = 0;
int pitch;
float spd = 0.5f;
float spd2 = 0.0f;
float sensitivity = 0.0f;
bool toggle = false;
bool capture = true;
bool effect = true;
bool toggle1 = false;
vec3 campos = {0.0f,0.0f,0.0f};
vec3 camrot = {0.0f,0.0f,0.0f};
vec3 camrot2 = {0.0f,0.0f,0.0f};
enum class enginestate {
    menu,
    loader,
    generator,
	rendering
};
enginestate currentstate =enginestate::menu;
enginestate laststate = currentstate;
uint64_t pm_delta = 0u;
constexpr float farDist= 8192.0f;
int parse_objtype(const char *s) {
    if (strcmp(s, "empty") == 0) return empty;
    if (strcmp(s, "cube") == 0) return cube;
    if (strcmp(s, "checkered") == 0) return checkered;
    if (strcmp(s, "light") == 0) return light;
	if (strcmp(s, "pointlight") == 0) return pointlight;
    if (strcmp(s, "spotlight") == 0) return spotlight;
	if (strcmp(s, "fog") == 0) return fog;
	if (strcmp(s, "portal") == 0) return portal;
	if (strcmp(s, "refract") == 0) return refract;
    if (strcmp(s, "sphere") == 0) return sphere;
	if (strcmp(s, "cylinder") == 0) return cylinder;
	if (strcmp(s, "bend") == 0) return bend;
	if (strcmp(s, "prism") == 0) return prism;
    return empty; // default
}
const char* get_type_string(int type) {
    switch (type) {
        case cube:       return "cube";
        case checkered:  return "checkered";
        case light:      return "light";
		case pointlight:      return "pointlight";
        case spotlight:      return "spotlight";
        case fog:        return "fog";
        case portal:     return "portal";
		case refract:    return "refract";
        case sphere:     return "sphere";
		case cylinder:     return "cylinder";
		case bend:      return "bend";
		case prism:      return "prism";
        case empty:      return "empty";
        default:         return "empty";
    }
}
float sky[bins] ={0.0f,0.0f,0.0f,0.0f,0.0f,0.0f};
void interpolatehalf(float *array){
	for (uint8_t loop = 0u; loop < bins; loop+=2u) {
		const float loop2 = 2+loop;
		array[(1u+loop)%bins] = (array[(0u+loop)%bins]+array[(loop2<bins-2)?loop:bins-2])*0.5f;
	}
}
void load_objects(const std::string& filename) {
    std::ifstream file(filename);
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue; // Skip comments/empty lines

        std::stringstream ss(line);
        std::string tag;
        
        // We create a temporary object with defaults
        object obj{}; 
		obj.type = empty;
		obj.roughness = obj.anisotropy = 0.5f;
		obj.rainbow = 0.01f;
		obj.scale.x = obj.scale.y = obj.scale.z = obj.ior = 1.0f;
		obj.color[0] = obj.color[1] = obj.color[2] = obj.color[3] = obj.color[4] = obj.color[5] = obj.color[6] = obj.color[7] = obj.color[8] = obj.color[9] = obj.color[10] = obj.color[11] = 250u;
		obj.rayleigh[0] = obj.rayleigh[1] = obj.rayleigh[2] = obj.rayleigh[3] = obj.rayleigh[4] = obj.rayleigh[5] = obj.rayleigh[6] = obj.rayleigh[7] = obj.rayleigh[8] = obj.rayleigh[9] = obj.rayleigh[10] = obj.rayleigh[11] = 1.0f;
		uint8_t mode = 0u;
        while (ss >> tag) {
            if (tag == "obj") {
                std::string type_str;
                ss >> type_str;
                obj.type = obj.type | parse_objtype(type_str.c_str());
				if (obj.type&pointlight){
					mode = 2;
				} else {
					mode = 1;
				}
            }
            else if (tag == "pos") ss >> obj.position.x >> obj.position.y >> obj.position.z;
            else if (tag == "rot") ss >> obj.rotation.x >> obj.rotation.y >> obj.rotation.z;
            else if (tag == "scale") ss >> obj.scale.x >> obj.scale.y >> obj.scale.z;
			else if (tag == "tpos") ss >> obj.position2.x >> obj.position2.y >> obj.position2.z;
            else if (tag == "color") {ss >> obj.color[0] >> obj.color[2] >> obj.color[4] >> obj.color[6] >> obj.color[8] >> obj.color[10];
				interpolatehalf(obj.color);
			}
			else if (tag == "color2") ss >> obj.color[0] >> obj.color[1] >> obj.color[2] >> obj.color[3] >> obj.color[4] >> obj.color[5] >> obj.color[6] >> obj.color[7] >> obj.color[8] >> obj.color[9] >> obj.color[10] >> obj.color[11];
            else if (tag == "anisotropy") ss >> obj.anisotropy;
			else if (tag == "ior") ss >> obj.ior;
			else if (tag == "refractpass") ss >> obj.refractpassness;
		    else if (tag == "rough" || tag == "density") ss >> obj.roughness;
			else if (tag == "specular") ss >> obj.specular;
			else if (tag == "specularrough") ss >> obj.specularroughness;
			else if (tag == "thickness") ss >> obj.thickness;
			else if (tag == "rainbow") ss >> obj.rainbow;
			else if (tag == "rayleigh") {ss >> obj.rayleigh[0] >> obj.rayleigh[2] >> obj.rayleigh[4] >> obj.rayleigh[6] >> obj.rayleigh[8] >> obj.rayleigh[10];
				interpolatehalf(obj.rayleigh);
			}
			else if (tag == "rayleigh2") ss >> obj.rayleigh[0] >> obj.rayleigh[1] >> obj.rayleigh[2] >> obj.rayleigh[3] >> obj.rayleigh[4] >> obj.rayleigh[5] >> obj.rayleigh[6] >> obj.rayleigh[7] >> obj.rayleigh[8] >> obj.rayleigh[9] >> obj.rayleigh[10] >> obj.rayleigh[11];
			else if (tag == "sky") {ss >> sky[0] >> sky[2] >> sky[4] >> sky[6] >> sky[8] >> sky[10];
				interpolatehalf(sky);
			}
			else if (tag == "sky2") ss >> sky[0] >> sky[1] >> sky[2] >> sky[3] >> sky[4] >> sky[5] >> sky[6] >> sky[7] >> sky[8] >> sky[9] >> sky[10] >> sky[11];
			printf("reading %s\n",tag.c_str());
		}
		for (uint_fast8_t i = 0; i < bins; ++i){
			sky[i] /= 255.0f;
			obj.color[i] /= 255.0f;
		}
		if(mode==1u){
			objects.push_back(obj);
		} else if (mode==2u){
			lights.push_back(obj);
		}
    }
}
void save_objects(const std::string& filename) {
    std::ofstream file(filename);
    auto write_object_to_file = [&](const auto& obj) {
		for (int i = 0; i < max_obj_type; ++i) {
			int bit = (1 << i);
			if (obj.type & bit) {
				file << "obj " << get_type_string(bit) << "\n";
			}
		}
		file << " pos " << obj.position.x << " " << obj.position.y << " " << obj.position.z << " rot " << obj.rotation.x << " " << obj.rotation.y << " " << obj.rotation.z
		 << " scale " << obj.scale.x << " " << obj.scale.y << " " << obj.scale.z
		 << " color2 " << obj.color[0] << " " << obj.color[1] << " " << obj.color[2] << " " << obj.color[3] << " " << obj.color[4] << " " << obj.color[5] << obj.color[6] << " " << obj.color[7] << " " << obj.color[8] << " " << obj.color[9] << " " << obj.color[10] << " " << obj.color[11]
		 << " tpos " << obj.position2.x << " " << obj.position2.y << " " << obj.position2.z
		 << " anisotropy " << obj.anisotropy
		 << " ior " << obj.ior 
		 << " refractpass " << obj.refractpassness
		 << ((obj.type & fog) ? " density " : " rough ") << obj.roughness
		 << " specular " << obj.specular
		 << " specularrough " << obj.specularroughness
		 << " thickness " << obj.thickness
		 << " rainbow " << obj.rainbow
		 << " rayleigh " << obj.rayleigh[0] << obj.rayleigh[1] << obj.rayleigh[2] << obj.rayleigh[3] << obj.rayleigh[4] << obj.rayleigh[5]
		 << "\n";
    };
	for (const object& obj : objects) {
		write_object_to_file(obj);
	}
	for (const object& lit : lights) {
		write_object_to_file(lit);
	}
    file.close();
}
#define TABLE_SIZE 16384
#define TABLE_MASK (TABLE_SIZE - 1)
constexpr float constsin(float x3) {
	float x = x3;
    while (x > M_PI) x -= M_PI2;
    while (x < -M_PI) x += M_PI2;

    long double x2 = x * x;
    long double res = x;
    long double term = x;
	long double flow = 6.0;
	for(size_t i = 0u; i < 128u; ++i){
		term *= -x2 / ((2*(i+1))*(2*(i+1)+1));
		res += term;
	}
    return static_cast<float>(res);
}
constexpr std::array<float, TABLE_SIZE> init_sin_table() {
	std::array<float,TABLE_SIZE> sin_table{0.0f};
    for (size_t i = 0; i < TABLE_SIZE; i++) {
        sin_table[i] = constsin(i * (M_PI / (TABLE_SIZE*0.5f))); // precompute
    }
	return sin_table;
}
static auto sin_table = init_sin_table();
const float radtodeg = ((TABLE_SIZE*0.5f)/M_PI);
float fast_sin(float radians) {
    float float_idx = radians * radtodeg;
    int idx_low = static_cast<int>(float_idx);
    return sin_table[idx_low & TABLE_MASK];
}
float fast_cos(float radians) {//convert number to rotation in y
    return fast_sin(radians + (M_PI * 0.5f));
}
inline float quick_sqrt(float x) {
    return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(x)));
}
inline uint32_t isqrt(uint32_t n) {
    uint32_t x = n;
    uint32_t c = 0;
    uint32_t d = 1u << 30;
    while (d > n) d >>= 2;
    while (d) {
        if (x >= c + d) {
            x -= c + d;
            c = (c >> 1) + d;
        } else {
            c >>= 1;
        }
        d >>= 2;
    }
    return c;
}
float fastsqrt(float x) {
    if (x == 0) return 0;
    float xhalf = 0.5f * x;
    int i = *(int*)&x;            // Store floating point bits as integer
    i = 0x5f3759df - (i >> 1);    // Magic number for 1/sqrt(x)
    float y = *(float*)&i;        // Convert bits back to float
    y = y * (1.5f - xhalf * y * y); // 1st Newton-Raphson iteration
    // y = y * (1.5f - xhalf * y * y); // Optional 2nd iteration for accuracy
    return x * y; // The "Inversion": x * (1/sqrt(x)) = sqrt(x)
}
typedef float (*SqrtFunc)(float);
static SqrtFunc my_sqrt = nullptr;
void init_math_system(bool hasSSE) {
    if (hasSSE) {
        my_sqrt = quick_sqrt;
    } else {
        my_sqrt = [](float x) { return sqrtf(x); }; 
    }
}
float signf(float x) {
    return std::signbit(x) ? -1.0f:1.0f;
}
vec3 normalize(vec3 v) {
    float len = my_sqrt((v.x*v.x + v.y*v.y + v.z*v.z));
    if (len > 0.0f) {
        float inv_len = 1.0/len;
        v.x *= inv_len; 
        v.y *= inv_len;
        v.z *= inv_len;
    }
    return v;
}
float dot(vec3 a, vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

vec3 cross(vec3 a, vec3 b) {
    return (vec3){
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}
inline float dist(vec3 a, vec3 b) {
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float dz = b.z - a.z;
    return my_sqrt(dx*dx + dy*dy + dz*dz);
}
inline uint32_t lowbias32(uint32_t& state) {
	uint32_t x = state;
    x = x ^ (x >> 16u);
    x = x * 0x7feb352du;
    x = x ^ (x >> 15u);
    x = x * 0x846ca68bu;
    x = x ^ (x >> 16u);
    state=x;
    return x;
}
inline uint32_t xorshift32(uint32_t& state) { // very fast
    uint32_t x = state;
    x ^= x << 13u;
    x ^= x >> 17u;
    x ^= x << 5u;
    state = x;
    return x;
}
inline uint32_t pcg_hash(uint32_t& state) { //real random number generator heavy
    uint32_t oldstate = state;
    state = oldstate * 747796405u + 2891336453u;
    uint32_t word = ((oldstate >> ((oldstate >> 28u) + 4u)) ^ oldstate) * 277803737u;
    return (word >> 22u) ^ word;
}
static inline float frand(uint32_t& state) {
    return (lowbias32(state) & 0xFFFF)*0.000030517578125f - 1.0f; // -1 to +1
}
static inline float ufrand(uint32_t& state) {
    return (lowbias32(state) & 0xFFFF)*0.00001525902189669642f; // -1 to +1
}
vec3 rotate(vec3 p, vec3 rot) {
    vec3 r = p;
    const float sx = fast_sin(rot.x);
    const float cx = fast_cos(rot.x);
	const float sy = fast_sin(rot.y);
	const float cy = fast_cos(rot.y);
	const float sz = fast_sin(rot.z);
	const float cz = fast_cos(rot.z);
    const float y1 = p.y*cx-p.z * sx;
    const float z2 = p.y*sx+p.z * cx;
    r.y = y1;
    r.z = z2;

    const float x2 = r.x*cz-r.y * sz;
    const float y2 = r.x*sz+r.y * cz;
    r.x = x2;
    r.y = y2;

    const float x1 = r.x*cy-r.z * sy;
    const float z1 = r.x*sy+r.z * cy;
    r.x = x1;
    r.z = z1;

    return r;
}
vec3 rotateodd(vec3 p, vec3 rot) {
    vec3 r = p;
    const float sx = fast_sin(rot.x);
    const float cx = fast_cos(rot.x);
	const float sy = fast_sin(rot.y);
	const float cy = fast_cos(rot.y);
	const float sz = fast_sin(rot.z);
	const float cz = fast_cos(rot.z);

    const float x1 = r.x*cy- r.z * sy;
    const float z1 = r.x*sy+r.z * cy;
    r.x = x1;
    r.z = z1;
	
    const float x2 = r.x*cz-r.y * sz;
    const float y2 = r.x*sz+r.y * cz;
    r.x = x2;
    r.y = y2;

    const float y1 = r.y*cx-r.z * sx;
    const float z2 = r.y*sx+r.z * cx;
    r.y = y1;
    r.z = z2;

    return r;
}

vec3 add(vec3 a, vec3 b) { 
	return (vec3){a.x+b.x, a.y+b.y, a.z+b.z}; 
}
vec3 sub(vec3 a, vec3 b) { 	
	return (vec3){a.x-b.x, a.y-b.y, a.z-b.z}; 
}
vec3 divec(vec3 a, vec3 b) {	
	return (vec3){a.x/(b.x), a.y/(b.y), a.z/(b.z)}; 
}
vec3 mul(const vec3 a,const vec3 b) { 
	return (vec3){a.x*b.x, a.y*b.y, a.z*b.z}; 
}
vec3 scale(vec3 a, float b) { 
	return (vec3){a.x*b, a.y*b, a.z*b}; 
}
vec3 randomsphere(uint32_t& state) {
	constexpr float M_PIdiv32768 = M_PI*0.000030517578125f;
	const float phi = M_PIdiv32768 * (lowbias32(state) & 0xFFFF);
    const float cos_theta = frand(state);
    const float sin_theta = my_sqrt(1.0f - cos_theta * cos_theta);
    return (vec3){
        sin_theta * fast_cos(phi),
        sin_theta * fast_sin(phi),
        cos_theta
    };
}
vec3 inv(vec3 a) { return (vec3){-a.x, -a.y, -a.z}; }
vec3 reflect(vec3 D, vec3 N) {
	return add(D, scale(N, -2.0f * (dot(D, N))));
}
constexpr float eps = 1e-32f;
inline bool bound(const vec3& scale, const vec3& rel, const vec3& ray) {
    const vec3 invRay = { 
        1.0f/(fabsf(ray.x) < eps ? (!std::signbit(ray.x) ? eps : -eps) : ray.x),
        1.0f/(fabsf(ray.y) < eps ? (!std::signbit(ray.y) ? eps : -eps) : ray.y),
        1.0f/(fabsf(ray.z) < eps ? (!std::signbit(ray.z) ? eps : -eps) : ray.z)
    };
    const vec3 t1 = mul(sub(scale, rel), invRay);
	const vec3 t0 = mul(sub(inv(scale), rel), invRay);
    const float tmin_x = fminf(t0.x, t1.x);
    const float tmax_x = fmaxf(t0.x, t1.x);
    const float tmin_y = fminf(t0.y, t1.y);
    const float tmax_y = fmaxf(t0.y, t1.y);
    const float tmin_z = fminf(t0.z, t1.z);
    const float tmax_z = fmaxf(t0.z, t1.z);
    const float t_near = fmaxf(fmaxf(tmin_x, tmin_y), tmin_z);
    const float t_far = fminf(fminf(tmax_x, tmax_y), tmax_z);
	return (t_near < t_far && !std::signbit(t_far));
}
void sdfsphere(const vec3& half,const vec3& rel,float *d){
	const float qx = rel.x/(half.x);
	const float qy = rel.y/(half.y);
	const float qz = rel.z/(half.z);
	const float qx1 = rel.x/(half.x);
	const float qy1 = rel.y/(half.y);
	const float qz1 = rel.z/(half.z);
	const float dSphere = my_sqrt(qx*qx + qy*qy + qz*qz);
	const float k = my_sqrt(qx1*qx1 + qy1*qy1 + qz1*qz1);
	*d = dSphere * (dSphere - 1.0f)/(k);
}
void sdfcube(const vec3& scale, vec3& rel, float *d){
	const vec3 q = (vec3){
		fabsf(rel.x) - scale.x,
		fabsf(rel.y) - scale.y,
		fabsf(rel.z) - scale.z
	};
	*d = fmaxf(fmaxf(q.x, q.y), q.z);
}
void distancecube(const vec3& box_scale,const vec3& rel,const vec3& ray, float *d) {
    const vec3 invRay = { 
        1.0f/(fabsf(ray.x) < eps ? (!std::signbit(ray.x) ? eps : -eps) : ray.x),
        1.0f/(fabsf(ray.y) < eps ? (!std::signbit(ray.y) ? eps : -eps) : ray.y),
        1.0f/(fabsf(ray.z) < eps ? (!std::signbit(ray.z) ? eps : -eps) : ray.z)
    };
    const vec3 t1 = mul(sub(box_scale, rel), invRay);
	const vec3 t0 = mul(sub(inv(box_scale), rel), invRay);
    const float tmin_x = fminf(t0.x, t1.x);
    const float tmax_x = fmaxf(t0.x, t1.x);
    const float tmin_y = fminf(t0.y, t1.y);
    const float tmax_y = fmaxf(t0.y, t1.y);
    const float tmin_z = fminf(t0.z, t1.z);
    const float tmax_z = fmaxf(t0.z, t1.z);
    const float t_near = fmaxf(fmaxf(tmin_x, tmin_y), tmin_z);
    const float t_far = fminf(fminf(tmax_x, tmax_y), tmax_z);
	if (t_near < t_far && !std::signbit(t_far))
		(std::signbit(t_near)) ? *d = 0.0f : *d = t_near;
}
void distancehollowcube(const vec3& box_scale,const vec3& rel,const vec3& ray, float *d) {
    const vec3 invRay = { 
        1.0f/(fabsf(ray.x) < eps ? (!std::signbit(ray.x) ? eps : -eps) : ray.x),
        1.0f/(fabsf(ray.y) < eps ? (!std::signbit(ray.y) ? eps : -eps) : ray.y),
        1.0f/(fabsf(ray.z) < eps ? (!std::signbit(ray.z) ? eps : -eps) : ray.z)
    };
    const vec3 t1 = mul(sub(box_scale, rel), invRay);
	const vec3 t0 = mul(sub(inv(box_scale), rel), invRay);
    const float tmin_x = fminf(t0.x, t1.x);
    const float tmax_x = fmaxf(t0.x, t1.x);
    const float tmin_y = fminf(t0.y, t1.y);
    const float tmax_y = fmaxf(t0.y, t1.y);
    const float tmin_z = fminf(t0.z, t1.z);
    const float tmax_z = fmaxf(t0.z, t1.z);
    const float t_near = fmaxf(fmaxf(tmin_x, tmin_y), tmin_z);
    const float t_far = fminf(fminf(tmax_x, tmax_y), tmax_z);
	if (t_near < t_far && !std::signbit(t_far)) {
		if (!std::signbit(t_near)) *d = t_near;
		else *d = t_far;
	}
}
constexpr vec3 p_norm1 = { 0.86602540378f, 0.5f, 0.0f }; 
constexpr vec3 p_norm2 = { -0.86602540378f, 0.5f, 0.0f }; 
void distanceprism(const vec3& box_scale,const vec3& rel,const vec3& ray, float *d) {
    const vec3 invRay = { 
        1.0f/(fabsf(ray.x) < eps ? (ray.x >= 0 ? eps : -eps) : ray.x),
        1.0f/(fabsf(ray.y) < eps ? (ray.y >= 0 ? eps : -eps) : ray.y),
        1.0f/(fabsf(ray.z) < eps ? (ray.z >= 0 ? eps : -eps) : ray.z)
    };

    const vec3 t1 = mul(sub(box_scale, rel), invRay);
    const vec3 t0 = mul(sub(inv(box_scale), rel), invRay);

    float tmin_x = fminf(t0.x, t1.x);
    float tmax_x = fmaxf(t0.x, t1.x);
    float tmin_y = fminf(t0.y, t1.y);
    float tmax_y = fmaxf(t0.y, t1.y);
    float tmin_z = fminf(t0.z, t1.z);
    float tmax_z = fmaxf(t0.z, t1.z);
	vec3 p_norm1scaled = normalize(mul(p_norm1,box_scale));
	vec3 p_norm2scaled = normalize(mul(p_norm2,box_scale));
    float n_dot_ray1 = dot(p_norm1scaled, ray);
    float n_dot_rel1 = dot(p_norm1scaled, rel);
    float n_dot_ray2 = dot(p_norm2scaled, ray);
    float n_dot_rel2 = dot(p_norm2scaled, rel);
	float p_origin_dist = box_scale.y * 0.39f;
    // Intersect the slanted plane
    if (fabsf(n_dot_ray1) > 1e-6f) {
        float t_slant = (p_origin_dist - n_dot_rel1) / n_dot_ray1;
        if (std::signbit(n_dot_ray1)) tmin_x = fmaxf(tmin_x, t_slant);
        else tmax_x = fminf(tmax_x, t_slant);
	} if (fabsf(n_dot_ray2) > 1e-6f) {
		float t_slant = (p_origin_dist - n_dot_rel2) / n_dot_ray2;
        if (std::signbit(n_dot_ray2)) tmin_x = fmaxf(tmin_x, t_slant);
        else tmax_x = fminf(tmax_x, t_slant);
    }
    float t_near = fmaxf(fmaxf(tmin_x, tmin_y), tmin_z);
    float t_far = fminf(fminf(tmax_x, tmax_y), tmax_z);

	if (t_near < t_far && !std::signbit(t_far)) {
		(!std::signbit(t_near))? *d = t_near:*d = t_far;
	}
}
void distancesphere(const vec3& sscale2,const vec3& rel,const vec3& ray, float *d) {
    const vec3 inv_s = { 1.0f/(sscale2.x), 1.0f/(sscale2.y), 1.0f/(sscale2.z) };
    const vec3 ro = mul(rel, inv_s); 
    const vec3 rd_local = mul(ray, inv_s); 
    const float a = dot(rd_local, rd_local);
    const float b = 2.0f * dot(ro, rd_local);
    const float c = dot(ro, ro) - 1.0f;
    const float disc = b * b - 4.0f * a * c;
    if (!std::signbit(disc)) {
        const float sqrt_d = my_sqrt(disc);
        const float t = (-b - sqrt_d)/(2.0f * a);
        if (!std::signbit(t)) {
            *d = t; 
        } else if (std::signbit(c)) {
			*d = 0.0f;
		}
    }
}
void distancehollowsphere(const vec3& sscale2,const vec3& rel,const vec3 &ray, float *d) {
    const vec3 inv_s = { 1.0f/(sscale2.x), 1.0f/(sscale2.y), 1.0f/(sscale2.z) };
    const vec3 ro = mul(rel, inv_s); 
    const vec3 rd_local = mul(ray, inv_s); 
    const float a = dot(rd_local, rd_local);
    const float b = 2.0f * dot(ro, rd_local);
    const float c = dot(ro, ro) - 1.0f;
    const float disc = b * b - 4.0f * a * c;
    if (!std::signbit(disc)) {
        const float sqrt_d = my_sqrt(disc);
        const float t = (-b - sqrt_d)/(2.0f * a);
		const float t1 = (-b + sqrt_d)/(2.0f * a);
        if (!std::signbit(t)) {
            *d = t; 
		} else if (std::signbit(c)) {
			*d = t1;
		}
    }
}
void distancecylinder(const vec3 &halfscale, const vec3& rel, const vec3& ray, float* d) {
    float invRayY = 1.0f / (fabsf(ray.y) < eps ? (std::signbit(ray.y) ? -eps : eps) : ray.y);
    float t0y = (-halfscale.y - rel.y) * invRayY;
    float t1y = (halfscale.y - rel.y) * invRayY;
    float tmin = fminf(t0y, t1y);
    float tmax = fmaxf(t0y, t1y);
    float inv_sx = 1.0f / halfscale.x;
    float inv_sz = 1.0f / halfscale.z;
    float rox = rel.x * inv_sx;
    float roz = rel.z * inv_sz;
    float rdx = ray.x * inv_sx;
    float rdz = ray.z * inv_sz;

    float a = rdx * rdx + rdz * rdz;
    float b = 2.0f * (rox * rdx + roz * rdz);
    float c = (rox * rox + roz * roz) - 1.0f;
    float disc = b * b - 4.0f * a * c;

    if (disc < 0.0f) {
        *d = 1e30f;
        return;
    }
    float sqrt_d = my_sqrt(disc);
    float t0_cyl = (-b - sqrt_d) / (2.0f * a);
    float t1_cyl = (-b + sqrt_d) / (2.0f * a);
    tmin = fmaxf(tmin, t0_cyl);
    tmax = fminf(tmax, t1_cyl);

    if (tmin <= tmax && !std::signbit(tmax)) {
        *d = (std::signbit(tmin)) ? 0.0f:tmin;
    }
}
void distancehollowcylinder(const vec3 &halfscale, const vec3& rel, const vec3& ray, float *d) {
    *d = farDist;
    const float invRayY = 1.0f / (fabsf(ray.y) < eps ? (std::signbit(ray.y) ? -eps : eps) : ray.y);
    float t_top = (halfscale.y - rel.y) * invRayY;
    float t_bot = (-halfscale.y - rel.y) * invRayY;

    for (float t_cap : {t_top, t_bot}) {
        if (!std::signbit(t_cap)) {
            float hitX = rel.x + ray.x * t_cap;
            float hitZ = rel.z + ray.z * t_cap;
			const float powx = hitX / halfscale.x;
			const float powz =hitZ / halfscale.z;
            if ((powx*powx + powz*powz) <= 1.0f) {
                if (t_cap < *d) *d = t_cap;
            }
        }
    }
    const float inv_sx = 1.0f / halfscale.x;
    const float inv_sz = 1.0f / halfscale.z;
    const float rox = rel.x * inv_sx, roz = rel.z * inv_sz;
    const float rdx = ray.x * inv_sx, rdz = ray.z * inv_sz;

    const float a = rdx * rdx + rdz * rdz;
    const float b = 2.0f * (rox * rdx + roz * rdz);
    const float c = (rox * rox + roz * roz) - 1.0f;
    const float disc = b * b - 4.0f * a * c;

    if (disc >= 0.0f) {
        const float sqrt_d = my_sqrt(disc);
        float t0 = (-b - sqrt_d) / (2.0f * a);
        float t1 = (-b + sqrt_d) / (2.0f * a);

        for (float t_side : {t0, t1}) {
            if (!std::signbit(t_side) && t_side < *d) {
                float hitY = rel.y + ray.y * t_side;
                if (fabsf(hitY) <= halfscale.y) {
                    *d = t_side;
                }
            }
        }
    }
}
static inline void aces(float &x,float &y,float &z) {
    constexpr float a = 2.51f; //2.51
    constexpr float b = 0.03f; //0.03
    constexpr float c = 2.43f; //2.43
    constexpr float d = 0.59f; //0.59
    constexpr float e = 0.14f; //0.14
	//i dont have the ue5 setting and i dont have ue5. ue5 has some good tonemappers. so i chose the default aces
	float num[3] = {x,y,z}; 
	for(uint8_t i = 0u; i < 3u; ++i){
		num[i] = (num[i] * ((a * num[i]) + b))/((num[i] * (c * num[i]) + d) + e);
	}
	x=num[0];
	y=num[1];
	z=num[2];
}
void my_tonemap(float &x,float &y,float &z){
	x=1.0f-(1.0f/(x+1.0f));
	y=1.0f-(1.0f/(y+1.0f));
	z=1.0f-(1.0f/(z+1.0f));
}
bool changed = false;
void raycast(const vec3 &localdir,const vec3 &rel,const object &a,const vec3 &halfscale, float *d){
	if (a.type & cube) {
		(a.type & refract)?
		distancehollowcube(halfscale,rel,localdir,d):
		distancecube(halfscale,rel,localdir,d);
	} else if (a.type & prism) {
		distanceprism(halfscale,rel,localdir,d);
	} else if (a.type & sphere) {
		(a.type & refract)?
		distancehollowsphere(halfscale,rel,localdir,d):
		distancesphere(halfscale,rel,localdir,d);
	} else if (a.type & cylinder) {
		(a.type & refract)?
		distancehollowcylinder(halfscale,rel,localdir,d):
		distancecylinder(halfscale,rel,localdir,d);
	}
}
constexpr float m[bins][3] = {
    { 0.52f, 0.04f, 0.01f }, // Red: Bleeds slightly into Green/Blue (Halation feel)
	{ 0.495f, 0.155f, 0.002f }, // orange
    { 0.37f, 0.27f, 0.003f }, // Yellow: Real yellow sensors have significant Red-Green overlap
    { 0.215f, 0.32f, 0.005f }, //yellow green
    { 0.06f, 0.37f, 0.09f }, // Green: Most sensitive channel2; bleeds into everything
	{ 0.035f, 0.415f, 0.2f},
    { 0.01f, 0.25f, 0.32f }, // Cyan: Shifts slightly toward Green to avoid "digital" cyan
	{ 0.025f, 0.16f, 0.335f},
    { 0.04f, 0.06f, 0.35f }, // Blue: High-energy blue bleeds into Red/Green (Purple tinting)
	{ 0.07f, 0.035f, 0.275f},
    { 0.10f, 0.01f, 0.20f },  // Indigo: Deep spectral blue/purple often triggers Red sensels
	{ 0.06f, 0.005f, 0.21f }
};
constexpr float m2[bins][3] = {
    { 0.384f, 0.0f, 0.0f }, // Red
	{ 0.288f, 0.0625f, 0.0f },
    { 0.192f, 0.125f, 0.0f }, // Yellow
	{ 0.096f, 0.1875f, 0.0f },
    { 0.0f, 0.25f, 0.0f }, // Green
	{ 0.0f, 0.1875f, 0.0625f },
    { 0.0f, 0.125f, 0.125f }, // Cyan
	{ 0.0f, 0.0625f, 0.1875f},
    { 0.0f, 0.0f, 0.25f }, // Blue
	{ 0.01f, 0.0f, 0.1875f },
    { 0.02f, 0.0f, 0.125f },  // Indigo
	{ 0.01f, 0.0f, 0.0625f }
};
int grabbedIdx = -1;
int aimcast = -1;
float distgrab = 0.0f;
int renderthread(void *ptr) {
	running = true;
	unsigned int timethread = 0u;
	float autoexposure2 = 1.0f;
	float autoexposure = 1.0f;
	float desiredExposure = 1.0f;
	vec3 offset = (vec3){0.0f,0.0f,0.0f};
	printf("running renderthread \n");
	constexpr float radtodeg = 180/M_PI;
	constexpr float degtorad = M_PI/180;
	std::vector<float> red(screensize, 0.0f);
	std::vector<float> green(screensize, 0.0f);
	std::vector<float> blue(screensize, 0.0f);
	std::vector<float> addcolor(screensize*bins,0);
	unsigned int sample = 0;
	vec3 fcampos=campos;
	vec3 fcamrot=camrot;
	vec3 flastpos=fcampos;
	vec3 flastrot=fcamrot;
	unsigned int total_cores = omp_get_num_procs();
	unsigned int cores = total_cores;
	total_cores = omp_get_num_procs();
	cores = total_cores;
	if (total_cores > 1u) {
		cores = total_cores - 1u;
	} else {
		cores = total_cores;
	}
	if (cores > 512u){
		cores = 512u;
	}
	omp_set_num_threads(cores);
	uint_fast32_t lastscreensize = width * height;
	float icolor = 1.0f;
	while(running) {
		const uint_fast32_t screensize = width * height;
		if (screensize!=lastscreensize){
			red.resize(screensize);
			green.resize(screensize);
			blue.resize(screensize);
			addcolor.resize(screensize*bins);
		}
		lastscreensize = screensize;
		const bool effect1 = effect;
		const float invMinDim = 1.0f / fminf(width, height);
		timethread++;
		const uint_fast16_t fsamples = samples,fbounces = bounces;
		const float desired_time = pm_render_ms*0.001f;
		const float tempexposure = (desired_time<1.0f)?1.0f:desired_time;
		const float signexposure = std::max(std::min(desiredExposure - autoexposure2,tempexposure),-tempexposure);
		autoexposure2 = autoexposure2 + signexposure;
		autoexposure = 0;
		SDL_LockMutex(render_mutex);
		const unsigned int object_size = objects.size();
		const unsigned int light_size = lights.size();
		flastpos=fcampos;
		flastrot=fcamrot;
		fcampos=campos;
		fcamrot=camrot2;
		SDL_UnlockMutex(render_mutex);
		vec3 hitscale = (vec3){0,0,0};
		std::vector<vec3> invrot(object_size,(vec3){0.0f,0.0f,0.0f}),halfscale(object_size,(vec3){0.0f,0.0f,0.0f}),degrees(object_size,(vec3){0.0f,0.0f,0.0f});
		std::vector<float> preciprocalfog(object_size,0.0f);
		#ifdef debug
			printf("precalculating objects \n");
		#endif
		for(unsigned int i = 0u; i < object_size; ++i){
			object* a = &objects[i];
			degrees[i].x=M_PI*a->rotation.x/180.0f;
			degrees[i].y=M_PI*a->rotation.y/180.0f;
			degrees[i].z=M_PI*a->rotation.z/180.0f;
			if(a->type & fog) preciprocalfog[i]=(1.0f-a->roughness)/(a->roughness);
			invrot[i] = inv(degrees[i]);
			halfscale[i] = scale(a->scale,0.5f);
		}
		int aimcast1 = -1;
		
		const vec3 rayDir = rotate((vec3){0.0f,0.0f, 1.0f}, fcamrot);
		float minDist = farDist;
		for (unsigned int i = 0u; i < object_size; ++i) {
			__builtin_prefetch(&objects[i+4], 0, 3);
			object* a = &objects[i];
			vec3 localdir = rotateodd(rayDir, invrot[i]);
			vec3 rel = rotateodd(sub(fcampos, a->position),invrot[i]);
			if (bound(a->scale,rel,localdir)) {
				float d = farDist;
				raycast(localdir,rel,*a,halfscale[i],&d);
				if (d < minDist) {
					aimcast1 = i;
					minDist = d;
				}
			}
		}
		aimcast=aimcast1;
		if (render_mutex && framebuffer){
			#pragma omp parallel
			{
				vec3 rayDir = (vec3){0.0f,0.0f,0.0f};
				unsigned int section = 0u;
				uint32_t state = timethread;
				#pragma omp for schedule(dynamic,16)
				for (uint_fast32_t idx = 0; idx < screensize; ++idx){
					if(!running)continue;
					const uint_fast32_t x = idx%width;
					const uint_fast32_t y = idx/width;
					const int_fast32_t sx = x - halfw,
					sy = halfh - y;
					const uint_fast32_t channel = idx*bins;
					for (sample = 0u; sample < fsamples; ++sample) {
						if(!running)break;
						const float motion = (lowbias32(state)& 0xFFF)*0.0001220703125f;
						const int_fast8_t bin1 = static_cast<int_fast8_t>(lowbias32(state)%bins);
						constexpr float fractbin = 1.0f/(bins+2); 
						const float float_bin = static_cast<float>(bin1+frand(state)+1.0f)*0.125f;
						
						const uint_fast8_t bin = (bin1<bins)?(bin1<0)?0u:bin1:bins-1u;
						const float nmlength = -float_bin*0.27f+0.75f;
						const float wavelength = (nmlength*nmlength);
						const uint_fast32_t channel2 = bin+channel;
						icolor = 1.0f;
						vec3 p = add(scale(fcampos,1.0f-motion),scale(flastpos,motion));
						rayDir = normalize(rotate((vec3){sx*invMinDim, sy*invMinDim, 1.0f}, add(scale(fcamrot,1.0f-motion),scale(flastrot,motion))));
						vec3 normal = (vec3){0.0f,0.0f,0.0f};
						vec3 localrel = (vec3){0.0f,0.0f,0.0f};
						for (uint_fast16_t bounce = 0u; bounce < fbounces; ++bounce) {
							rayDir = normalize(rayDir);
							float minDist = farDist;
							bool hit = false;
							for (unsigned int i = 0u; i < object_size; ++i) {
								__builtin_prefetch(&objects[i+4], 0, 3);
								vec3 localdir = rotateodd(rayDir, invrot[i]);
								vec3 rel = rotateodd(sub(p, objects[i].position),invrot[i]);
								if (!bound(objects[i].scale,rel,localdir)) continue;
								float d = farDist;
								raycast(localdir,rel,objects[i],halfscale[i],&d);
								if (objects[i].type & fog){
									const float randomfog = ufrand(state);
									float d3 = ((std::signbit(d))?0.0f:d)+(randomfog)*preciprocalfog[i];
									float d2 = farDist;
									rel = rotateodd(sub(add(p, scale(rayDir,d3)), objects[i].position),invrot[i]);
									if (objects[i].type & cube) {sdfcube(halfscale[i],rel,&d2);} else if (objects[i].type & sphere) {sdfsphere(halfscale[i],rel,&d2);}
									if (std::signbit(d2) && d3 < minDist){section = i; minDist = d3; hit = true;}
								} else if (objects[i].type & bend && d < minDist) {
									section = i; minDist = d+1.0f; hit = true;
								} else if (d < minDist) {
									section = i; minDist = d; hit = true;
								}
							}
							if (hit) {
								float misroughness = objects[section].roughness;
								vec3 flection = (vec3){0.0f,0.0f,0.0f};
								localrel = sub(add(p, scale(rayDir,minDist)), objects[section].position);
								vec3 hitrel = rotateodd(localrel,invrot[section]);
								if(objects[section].type & cube){
									vec3 box = divec((vec3){fabsf(hitrel.x),fabsf(hitrel.y),fabsf(hitrel.z)},objects[section].scale);
									if (box.x > box.y && box.x > box.z) normal = (vec3){ signf(hitrel.x), 0.0f, 0.0f };
									else if (box.y > box.z) normal = (vec3){ 0.0f, signf(hitrel.y), 0.0f };
									else normal = (vec3){ 0.0f, 0.0f, signf(hitrel.z) };
								} else if (objects[section].type & prism) {
									float boxx = (hitrel.x)/objects[section].scale.x;
									float boxy = (hitrel.y)/objects[section].scale.y;
									float boxz = (hitrel.z)/objects[section].scale.z;
									float boxxs = fabsf(boxx);
									float boxys = fabsf(boxy);
									float boxzs = fabsf(boxz);
									if (0.4999f>boxys&&0.4999f>boxzs){std::signbit(hitrel.x)? normal = p_norm2:normal = p_norm1;}
									else if (boxy < -0.4999f) normal = (vec3){ 0.0, -1.0f, 0.0};
									else normal = (vec3){ 0.0f, 0.0f, signf(hitrel.z) };
									normal = normalize(divec(normal, objects[section].scale));
								} else if (objects[section].type & sphere){
									normal = normalize(divec(hitrel, mul(objects[section].scale,objects[section].scale))); // divides by scale		
								} else if (objects[section].type & cylinder) {
									float normalizedY = fabsf(hitrel.y)/(objects[section].scale.y);
									if (normalizedY >= 0.4999f) {
										normal = (vec3){ 0.0f, signf(hitrel.y), 0.0f };
									} else {
										vec3 objectSpaceNormal = (vec3){ hitrel.x/(objects[section].scale.x * objects[section].scale.x), 0.0f, hitrel.z/(objects[section].scale.z * objects[section].scale.z) };
										normal = normalize(objectSpaceNormal);
									}
								}
								normal = rotate(normal, degrees[section]);
								p = add(p, scale(rayDir,minDist));
								float tempcolor = icolor*objects[section].color[bin];
								if(objects[section].type & light){
									icolor = tempcolor;
									addcolor[channel2] += icolor;
									break;
								} else if(objects[section].type & spotlight){
									icolor = tempcolor*(dot(rotate((vec3){0.0f,1.0f,0.0f},degrees[section]),rayDir));
									if (!std::signbit(icolor)){
										addcolor[channel2] += icolor;
									}
									break;
								}
								if (objects[section].type & fog){
									icolor = tempcolor;
									const float mag = (1.0f-objects[section].anisotropy)*objects[section].rayleigh[bin];
									misroughness = mag;
									flection = rayDir;
									rayDir = add(rayDir,scale(randomsphere(state),mag));
								} else {
									const float invroughness = 1.0-objects[section].roughness;
									float cosi = dot(rayDir, normal);
									if (objects[section].type & refract) {
										icolor = tempcolor;
										float etai = 1.0f, etat = objects[section].ior*(1.0+objects[section].rainbow*wavelength);
										if (!std::signbit(cosi)) { 
											std::swap(etai, etat); 
											normal = inv(normal); 
										} else {
											cosi = -cosi;
										}
										const float eta = etai/(etat);
										const float cosi2 = cosi*cosi;
										const float eta2 = eta*eta;
										float k = 1.0f - eta2 * (1.0f - cosi2);
										if (k<ufrand(state)&&objects[section].refractpassness>ufrand(state)) {
											flection = reflect(rayDir, normal);
											rayDir = add(scale(rayDir,invroughness),scale(randomsphere(state),objects[section].roughness));
											if (std::signbit(dot(rayDir, normal))) rayDir = reflect(rayDir,normal);
											const float phase = 1.0f+fast_cos((M_PI2 * objects[section].thickness * (1.0f-cosi) )*wavelength);
											icolor*=((phase>1.0f)?1.0f:phase);
										} else {
											const vec3 refractnormal = scale(normal, eta * cosi - my_sqrt(k));
											flection = add(scale(rayDir, eta), refractnormal);
											rayDir = flection;
											rayDir = add(scale(rayDir,invroughness),scale(randomsphere(state),objects[section].roughness));
										}
										if (objects[section].type & checkered)if (static_cast<int8_t>(floorf(hitrel.x)+floorf(hitrel.y)+floorf(hitrel.z)) & 1) icolor*=0.5f;
									} else if (objects[section].type & bend) {
										icolor = tempcolor;
										const float bending = objects[section].ior+objects[section].rainbow*wavelength;
										const vec3 relative_scale = divec(hitrel,objects[section].scale);
										const float dotted = dot(relative_scale,relative_scale);
										const float sqrdist = 1.0/dotted;
										const float pull_strength = bending*sqrdist;
										rayDir = add(rayDir, scale(normal,-pull_strength));
										flection = rayDir;
										rayDir = add(scale(rayDir,invroughness),scale(randomsphere(state),objects[section].roughness*2.0f));
										if (objects[section].type & checkered)if (static_cast<int8_t>(floorf(hitrel.x)+floorf(hitrel.y)+floorf(hitrel.z)) & 1) icolor*=0.5f;
									} else if(objects[section].type & portal){
										icolor = tempcolor;
										p = add(objects[section].position2, p);
										flection = rayDir;
										rayDir = add(scale(rayDir,invroughness),scale(randomsphere(state),objects[section].roughness*2.0f));
										if (objects[section].type & checkered)if (static_cast<int8_t>(floorf(hitrel.x)+floorf(hitrel.y)+floorf(hitrel.z)) & 1) icolor*=0.5f;
									} else {
										flection = reflect(rayDir,normal);
										const float cosi2 = cosi*cosi;
										const float phase = 1.0f + fast_cos((M_PI2 * objects[section].thickness * (1.0f-cosi) )*wavelength);
										if (-cosi>ufrand(state)||objects[section].specular<ufrand(state)){
											icolor = tempcolor;
											rayDir = add(scale(rayDir,invroughness),scale(randomsphere(state),objects[section].roughness*2.0f));
											if (std::signbit(dot(rayDir, normal))) rayDir = reflect(rayDir,normal);
											if (objects[section].type & checkered)if (static_cast<int8_t>(floorf(hitrel.x)+floorf(hitrel.y)+floorf(hitrel.z)) & 1) icolor*=0.5f;
										} else {
											misroughness=objects[section].specularroughness;
											rayDir = add(scale(rayDir,1.0f-objects[section].specularroughness),scale(randomsphere(state),objects[section].specularroughness));
											if (std::signbit(dot(rayDir, normal))) rayDir = reflect(rayDir,normal);
										}
										icolor*=((phase>1.0f)?1.0f:phase);
									}
								}
								p = add(p,scale(rayDir,minreq));
								for(unsigned int i2 = 0u; i2 < light_size; ++i2){
									const vec3 point = add(lights[i2].position,mul(randomsphere(state),scale(lights[i2].scale,(lowbias32(state)& 0xFFF)*0.0001220703125f)));
									minDist = dist(point,p);
									const float *intensity = &lights[i2].color[bin];
									
									hit = false;
									vec3 pointDir=sub(point,p);
									const vec3 npointDir = normalize(pointDir);
									
									const float angleflection =dot(normalize(flection),npointDir);
									float anglenormal =dot(normal,npointDir);
									const float invmisroughness = 1.0f-misroughness;
									const float dense = (objects[section].type & fog)?angleflection:angleflection*invmisroughness+anglenormal*misroughness;
									if (!std::signbit(dense)){
										for (unsigned int i = 0u; i < object_size; ++i) {
											__builtin_prefetch(&objects[i+4], 0, 3);
											vec3 localdir = rotateodd(npointDir, invrot[i]);
											vec3 rel = rotateodd(sub(p, objects[i].position),invrot[i]);
											if (!bound(objects[i].scale,rel,localdir)) continue;
											float d = farDist;//limited distance
											raycast(localdir,rel,objects[i],halfscale[i],&d); //they now are being calculating distance of that objects surface
											if (objects[i].type & fog){
												const float randomfog = ufrand(state);
												float d3 = ((std::signbit(d))?0.0f:d)+(randomfog)*preciprocalfog[i];
												float d2 = farDist;
												rel = rotateodd(sub(add(p, scale(npointDir,d3)), objects[i].position),invrot[i]);
												if (objects[i].type & cube) {sdfcube(halfscale[i],rel,&d2);} else if (objects[i].type & sphere) {sdfsphere(halfscale[i],rel,&d2);}
												if (std::signbit(d2) && d3 < minDist){hit = true; break;}
											}
											else if (d < minDist) {hit = true; break;}
										}
										if (!hit){ //no obstacle hit cast light
											if (misroughness <0.001f) misroughness =0.001f;
											const float shininess = 1.0/(misroughness);
											const float attenuation = *intensity/(minDist*minDist+1.0f);
											const float temp = icolor*powf(dense,shininess)*((shininess+2.0)*12.5f)*attenuation;
											if (!std::signbit(temp)){
												addcolor[channel2] += temp;
											}
										}
									}
								}
							} else { //nothing hit?
								addcolor[channel2] += icolor*sky[bin];
								break;
							}
							if (icolor<0.01f) break;
						}//bounce
					} //samples
				}//xy
			}//parallel
			SDL_LockMutex(render_mutex);
			#ifdef debug
				printf("rendering \n");
			#endif

			const uint32_t intMinDim = std::min(width, height);
			if (effect1){ //spectral to rgb converter
				for(uint_fast32_t idx = 0u; idx < screensize; ++idx){
					for(uint_fast32_t a = 0u; a < bins; ++a){
						uint_fast32_t channel2 = a+idx*bins;
						red[idx] += (addcolor[channel2] * m[a][0]); //this m is for red sensor instensity
						green[idx] += (addcolor[channel2] * m[a][1]); //this m is for green sensor instensity
						blue[idx] += (addcolor[channel2] * m[a][2]); //this m is for blue sensor instensity
						addcolor[channel2] = 0.0f;
					} //color
				}
			} else { //spectral to rgb converter
				for(uint_fast32_t idx = 0u; idx < screensize; ++idx){
					for(uint_fast32_t a = 0u; a < bins; ++a){
						uint_fast32_t channel2 = a+idx*bins;
						red[idx] += (addcolor[channel2] * m2[a][0]); //m2 is for red sensor instensity
						green[idx] += (addcolor[channel2] * m2[a][1]);
						blue[idx] += (addcolor[channel2] * m2[a][2]);
						addcolor[channel2] = 0.0f;
					} //color
				} 
			}
			const float total = 1.0/(fsamples*cores);
			for(uint_fast32_t idx = 0u; idx < screensize; ++idx){
				red[idx] *=total;
				green[idx] *= total;
				blue[idx] *=total;
				autoexposure += fminf(red[idx]+green[idx]+blue[idx],12.0f);
				const float powexposure = autoexposure2*autoexposure2;
				red[idx] = red[idx]*powexposure;
				green[idx] = green[idx]*powexposure;
				blue[idx] = blue[idx]*powexposure;
				if (effect1){
					aces(red[idx],green[idx],blue[idx]);
				}
					powf(red[idx],0.454545f);
					powf(green[idx],0.454545f);
					powf(blue[idx],0.454545f);
				red[idx]*=255.0f;
				green[idx]*=255.0f;
				blue[idx]*=255.0f;
				pixels[idx * 3 + 0] = (uint8_t)(red[idx]>255.0f)?255u:(red[idx]<0.0f)?0u:red[idx];
				pixels[idx * 3 + 1] = (uint8_t)(green[idx]>255.0f)?255u:(green[idx]<0.0f)?0u:green[idx];
				pixels[idx * 3 + 2] = (uint8_t)(blue[idx]>255.0f)?255u:(blue[idx]<0.0f)?0u:blue[idx];
				red[idx] = 0.0f;
				green[idx] = 0.0f;
				blue[idx] = 0.0f;
			}
			changed = true;
			SDL_UnlockMutex(render_mutex);
			#ifdef debug
				printf("finished rendering \n");
			#endif
			const float avgLuminance = 3.0f*(screensize/autoexposure);
			desiredExposure = my_sqrt(avgLuminance);
			if (desiredExposure < 0.05f) desiredExposure = 0.05f;
			if (desiredExposure > 24.0f) desiredExposure = 24.0f;
		}//RENDER MUTEX an FRAMEBUFFER
	}
	SDL_Log("Thread finished!");
	return 0;
}
namespace fs = std::filesystem;
fs::path exeDir;
std::vector<std::string> foundFiles;
void find_files() {
    std::vector<std::string> localfoundFiles;
    std::cout << "Scanning for .txt files in: " << exeDir << "\n";
    // 2. Iterate through every file in that folder
    for (const auto& entry : fs::directory_iterator(exeDir)) {
        // 3. Check if it's a regular file and ends in .txt
        if (entry.is_regular_file() && entry.path().extension() == ".txt") {
            localfoundFiles.push_back(entry.path().filename().string());
        }
    }
    // 4. Show what we found
    if (localfoundFiles.empty()) {
        std::cout << "no .txt files found!" << std::endl;
    } else {
        for (const auto& name : localfoundFiles) {
            std::cout << "Found scene file: " << name << std::endl;
        }
		foundFiles = localfoundFiles;
    }
}
bool loadandrun = false;
bool Tbug = true;
void resetapp(SDL_AppResult &ret){
	SDL_Log("AppInit started");
	running = false;
	currentstate = enginestate::loader;
	Tbug = true;
	loadandrun = false;
	find_files();
	bounces = 8;
	samples = 6;
	iterations = 0;
	pitch = 0;
	spd = 0.5f;
	toggle = false;
	capture = true;
	toggle1 = false;
	effect = true;
	sensitivity = 0.04f;
	campos = {0.0f,4.0f,0.0f};
	camrot = {0.0f,0.0f,0.0f};
	camrot2 = {0.0f,0.0f,0.0f};
	grabbedIdx = -1;
	aimcast = -1;
	objects.clear();
	lights.clear();
    // Create mutex for thread synchronization
	if (!render_mutex) {
		render_mutex = SDL_CreateMutex();
	}
    if (!render_mutex) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create mutex: %s", SDL_GetError());
		getchar();
        ret = SDL_APP_FAILURE;
    }
    SDL_SetAppMetadata("game","1.0","com.noob.game");
    // Fixed SDL initialization check
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
		getchar();
        ret = SDL_APP_FAILURE;
    }
    SDL_Log("SDL initialized");
	std::cout<<"change samples '- +' \n"<<"change bounce '0 9' \n";
    // Create window and renderer
	if (!(renderer || window)) {
		SDL_CreateWindowAndRenderer("spectral path tracer render C++", screenWidth, screenHeight, SDL_WINDOW_OPENGL, &window, &renderer);
	}
    if (!(renderer || window)) {
        SDL_Log("Couldn't create window/renderer: %s", SDL_GetError());
		getchar();
        ret = SDL_APP_FAILURE;
	}
    SDL_SetWindowAlwaysOnTop(window, true);
    // Create framebuffer texture
	if (!framebuffer) {
		framebuffer = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, width, height);
	}
    if (!framebuffer) {
        SDL_Log("Failed to create framebuffer: %s", SDL_GetError());
		getchar();
        ret = SDL_APP_FAILURE;
    }
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	//lock mouse
	SDL_Log("locking mouse");
    SDL_SetRenderVSync(renderer, false);
    SDL_SetWindowRelativeMouseMode(window, true);
	SDL_SetTextureScaleMode(framebuffer, SDL_SCALEMODE_NEAREST);
    SDL_Log("AppInit completed");
	SDL_SetWindowAlwaysOnTop(window, false);
}
SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
	exeDir = fs::absolute(argv[0]).parent_path();
	cpu = check_cpu();
	init_math_system(cpu.hasSSE41);
	SDL_AppResult ret = SDL_APP_CONTINUE;
	resetapp(ret);
    return ret;
}
void reset(SDL_AppResult &ret2) {
    SDL_Log("quitting renderer \n");
	running = false;
	if (thread) {
		SDL_UnlockTexture(framebuffer);
		SDL_UnlockMutex(render_mutex);
		SDL_WaitThread(thread, &renderthreadreturnvalue);
		SDL_Log("Thread returned value: %d", renderthreadreturnvalue);
	}
	SDL_UnlockMutex(render_mutex);
	SDL_UnlockMutex(render_mutex);
	SDL_AppResult ret = SDL_APP_CONTINUE;
	resetapp(ret);
	ret2 = SDL_APP_CONTINUE;
}
void Quit(SDL_AppResult &ret2) {
    SDL_Log("quitting app \n");
	running = false;
	if (thread) {
		SDL_UnlockTexture(framebuffer);
		SDL_UnlockMutex(render_mutex);
		SDL_WaitThread(thread, &renderthreadreturnvalue);
		SDL_Log("Thread returned value: %d", renderthreadreturnvalue);
	}
	SDL_UnlockMutex(render_mutex);
	SDL_UnlockMutex(render_mutex);
	if (renderer) SDL_DestroyRenderer(renderer);
	if (window) SDL_DestroyWindow(window);
	ret2 = SDL_APP_SUCCESS;
}
void save_bmp(std::string path, int w, int h, uint8_t *pixels) {
    std::ofstream f(path,std::ios::binary);
    // BMP Header (Standard 54 bytes)
    unsigned char header[54] = {'B','M', 0,0,0,0, 0,0,0,0, 54,0,0,0, 40,0,0,0, 0,0,0,0, 0,0,0,0, 1,0, 24,0};
    int size = 54 + (3 * w * h);
    *(int*)&header[2] = size;
    *(int*)&header[18] = w;
    *(int*)&header[22] = h;

    f.write((char*)header, 54);

    // Write pixels (BMP is BGR order and bottom-to-top)
    for (int y = h - 1; y >= 0; --y) {
        for (int x = 0; x < w; ++x) {
            int i = y * w + x;
            // Normalize uint64 to 0-255
            unsigned char blue  = (unsigned char)(pixels[i*3+2]);
            unsigned char green = (unsigned char)(pixels[i*3+1]);
            unsigned char red   = (unsigned char)(pixels[i*3+0]);
            
            f.put(blue); f.put(green); f.put(red);
        }
    }
}
int selectcpp = 0;
uint32_t state3 = 1890;
bool run = false;
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
	switch (event->type) {
		case SDL_EVENT_KEY_DOWN: {
			// Check if this is the initial press, not a held-down repeat
			if (event->key.down && !event->key.repeat) {
				if (currentstate==enginestate::loader){
					if (event->key.scancode == SDL_SCANCODE_UP) {
						selectcpp = std::max(0,selectcpp-1);
					}
					else if (event->key.scancode == SDL_SCANCODE_DOWN) {
						selectcpp = std::min(std::max(static_cast<int>(foundFiles.size())-1,0),selectcpp+1);
					}
					else if (event->key.scancode == SDL_SCANCODE_RETURN) {
						loadandrun = true;
					}
				} else {
					if (event->key.scancode == SDL_SCANCODE_T) {
						Tbug = !Tbug;
					}
					if (event->key.scancode == SDL_SCANCODE_F2) {
						// Trigger your BMP save function here!
						static int frame_count = 0;
						std::string path = exeDir.string()+"/render"+std::to_string(frame_count)+".bmp";
						save_bmp(path, width, height, pixels);
						printf("screenshot saved");
						frame_count++;
					}
				}
				if (event->key.scancode==SDL_SCANCODE_ESCAPE) {
					capture = !capture;
					SDL_SetWindowRelativeMouseMode(window, capture);
				}
			}
			if (event->key.scancode == SDL_SCANCODE_LSHIFT) {
				run = true;
			}
			break;
		}
		case SDL_EVENT_KEY_UP: {
			if (event->key.scancode == SDL_SCANCODE_LSHIFT) {
				run = false;
			}
			break;
		}
		case SDL_EVENT_MOUSE_MOTION: {
			if (capture && currentstate==enginestate::rendering) {
				camrot.y -= ((float)event->motion.xrel) * sensitivity *0.1f;
				camrot.x = SDL_max(-1.45f, SDL_min(1.45f, camrot.x + ((int)event->motion.yrel) * (sensitivity *0.05f)));
			}
			break;
		}
		case SDL_EVENT_MOUSE_BUTTON_DOWN: {
			if (event->button.button == SDL_BUTTON_LEFT && capture) {
				if (grabbedIdx != -1) {
					// RELEASE: We are already holding something, so let go.
					#ifdef debug
						printf("Released object %d\n", grabbedIdx);
					#endif
					grabbedIdx = -1;
				} else {
					// GRAB: Check if our raycast hit an object.
					// Assuming 'aimcast' stores the index of the object hit by the center pixel.
					if (aimcast != -1) {
						grabbedIdx = aimcast;
						#ifdef debug
							printf("Grabbed object %d\n", grabbedIdx);
						#endif
					}
				}
			}
			break;
		}
		case SDL_EVENT_MOUSE_WHEEL: {
			if (grabbedIdx != -1) {
				float wheelY = event->wheel.y;
					if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
						wheelY *= -1.0f;
					}
				if (wheelY > 0.0f) {
					distgrab *= 1.1f;
				} else if (wheelY < 0.0f) {
					distgrab /= 1.1f;
				}
			}
			break;
		}
	}
	if (event->type == SDL_EVENT_QUIT) {
		SDL_AppResult ret = SDL_APP_SUCCESS;
		if(running){
			reset(ret);
		} else {
			Quit(ret);
		}
		return ret;
    }
	return SDL_APP_CONTINUE;
}
vec3 move2 = {0.0f,0.0f,0.0f};

SDL_AppResult SDL_AppIterate(void *appstate) {
    const bool *key_states = SDL_GetKeyboardState(NULL);
	char debug_info[128] = "";
    static uint64_t last = 0;
    const uint64_t target_ns = 16666666;  // 1/60 second = 16.666 ms
    uint64_t now = SDL_GetTicksNS();
    if (last == 0) last = now;            // first frame init
    uint64_t elapsed = now - last;
	iterations++;
    if (elapsed < target_ns) {
        SDL_DelayNS(target_ns - elapsed); // sleep the remaining time
        last += target_ns;                // fixed step
    } else {
        last = now;   // frame was slow; catch up
    }
    // Movement controls
	const float delta = fmaxf(static_cast<float>(elapsed)/16666666.0f,1.0f);
	vec3 move = {0.0f,0.0f,0.0f};
	if (run){
		spd2 = 0.2f;
	} else {
		spd2 = 0.1f;
	}
	spd += (spd2-spd)*0.125f;
	if (currentstate==enginestate::loader){
		if (laststate==enginestate::menu){
			selectcpp=0;
		}
		int maxfilesize = foundFiles.size()-1;
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		// Clear the screen with the color set above
		SDL_RenderClear(renderer);
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 128);
		SDL_FRect rect3 = {8,8,screenWidth-16,102};
		SDL_RenderFillRect(renderer, &rect3);
		SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255); 
		for (int i = -4; i != 4; ++i){
			if(selectcpp+i<foundFiles.size() && selectcpp+i>=0){
			SDL_snprintf(debug_info, sizeof(debug_info), (i==0)?"selected file: (%i). %s":"               (%i). %s",selectcpp+i, foundFiles[selectcpp+i].c_str());
			SDL_RenderDebugText(renderer, 10, 50+(i*10), debug_info);
			}
		}
		SDL_RenderDebugText(renderer, 10, 100, "press | ^ | v | Enter |");
		
		if (loadandrun){
			printf("loading \n");
			std::string selectedfile = foundFiles[selectcpp];
			std::cout << "Attempting to open '" << foundFiles[selectcpp] << "'" << std::endl;
			std::string fullpath = exeDir.string()+"/"+selectedfile.c_str(); 
			load_objects(fullpath);
			thread = SDL_CreateThread(renderthread, "depthrender", NULL);
			if (thread == NULL) {
				SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateThread failed: %s", SDL_GetError());
				getchar();
				return SDL_APP_FAILURE;
			}
			currentstate = enginestate::rendering;
			loadandrun = false;
		}
	} else if (currentstate==enginestate::rendering){
		bool bounce = false;
		if (capture){
			if (key_states[SDL_SCANCODE_P]) {
				if (!toggle1) {
					effect = !effect;
					printf("changed effect %d\n",effect);
					toggle1 = true;
				}
			} else {
				toggle1 = false;
			}
			if (key_states[SDL_SCANCODE_W]) {
				move = add(rotate((vec3) {0.0f,0.0f,1.0f},(vec3) {0.0f,camrot.y,0.0f}),move);
			}
			if (key_states[SDL_SCANCODE_S]) {
				move = add(rotate((vec3) {0.0f,0.0f,-1.0f},(vec3) {0.0f,camrot.y,0.0f}),move);
			}
			if (key_states[SDL_SCANCODE_A]) {
				move = add(rotate((vec3) {-1.0f,0.0f,0.0f},(vec3) {0.0f,camrot.y,0.0f}),move);
			}
			if (key_states[SDL_SCANCODE_D]) {
				move = add(rotate((vec3) {1.0f,0.0f,0.0f},(vec3) {0.0f,camrot.y,0.0f}),move);
			}
			if (key_states[SDL_SCANCODE_Q]) {
				move = add(rotate((vec3) {0.0f,-1.0f,0.0f},(vec3) {0.0f,camrot.y,0.0f}),move);
			}
			if (key_states[SDL_SCANCODE_E]) {
				move = add(rotate((vec3) {0.0f,1.0f,0.0f},(vec3) {0.0f,camrot.y,0.0f}),move);
			}
			if (key_states[SDL_SCANCODE_RIGHT]) {
				camrot.y -= sensitivity*delta; 
			}
			if (key_states[SDL_SCANCODE_LEFT]) {
				camrot.y += sensitivity*delta; 
			}
			if (key_states[SDL_SCANCODE_UP]) {
				camrot.x -= sensitivity*delta; 
			}
			if (key_states[SDL_SCANCODE_DOWN]) {
				camrot.x += sensitivity*delta; 
			}
			if (key_states[SDL_SCANCODE_EQUALS]) {
				if (samples < (uint_fast16_t){65535}){
					samples += (uint_fast16_t){1}; 
				}
				bounce = true;
			}
			if (key_states[SDL_SCANCODE_MINUS]) {
				if (samples > (uint_fast16_t){1}){
					samples -= (uint_fast16_t){1}; 
				}
				bounce = true;
			}
			if (key_states[SDL_SCANCODE_0]) {
				if (bounces < (uint_fast16_t){65535}){
					bounces += (uint_fast16_t){1}; 
				}
				bounce = true;
			}
			if (key_states[SDL_SCANCODE_9]) {
				if (bounces > (uint_fast16_t){1}){
					bounces -= (uint_fast16_t){1}; 
				}
				bounce = true;
			}
		}
		static float shake[3] = {0.0f,0.0f,0.0f};
		static float shake1[3] = {0.0f,0.0f,0.0f};
		float temp = 0.2f;
		for (uint8_t i = 0; i<3;++i){
			shake[i]=shake[i]*(1.0f-temp)+frand(state3)*temp;
			shake1[i]=shake1[i]*(1.0f-temp)+shake[i]*temp;
		}
		
		camrot.x = SDL_max(-1.45f, SDL_min(1.45f, camrot.x ));
		move2 = add(move2,scale(sub(scale((vec3)normalize(move),delta),move2),0.1f));
		camrot2=add(camrot2,scale(sub(add(camrot,(vec3){shake1[0]*0.1f,shake1[1]*0.05f,shake1[2]*0.1f}),camrot2),0.1f));
		campos = add(scale(move2,spd),campos);
		static bool grabbed = false;
		if (grabbedIdx != -1) {
			const vec3 degrot = (vec3){camrot.x*radtodeg,camrot.y*radtodeg,camrot.z*radtodeg};
			if (!grabbed){
				const vec3 diff = sub(campos,objects[grabbedIdx].position);
				distgrab = my_sqrt(dot(diff,diff));
			}
			// Calculate forward vector from your camera rotation
			vec3 forward =rotate((vec3){0.0f,0.0f,distgrab},camrot);
			// Update the object position
			
			objects[grabbedIdx].position = add(campos,forward);
		}
		grabbed = (grabbedIdx != -1);
		// Render
		SDL_FRect rect = {0,0,screenWidth,screenHeight};
		if (framebuffer){
			SDL_LockMutex(render_mutex);
			SDL_LockTexture(framebuffer, NULL, (void**)&pixels, &pitch);
			SDL_RenderTexture(renderer, framebuffer, NULL,&rect);
			SDL_UnlockMutex(render_mutex);
			SDL_UnlockTexture(framebuffer);
		}
		static uint64_t pm_last_frame_tick = 0; 
		uint64_t current_tick = SDL_GetTicks();
		if (changed) {
			// Calculate how long since the last time the marcher 'changed'
			pm_delta = current_tick - pm_last_frame_tick;
			
			if (pm_delta > 0) {
				pm_render_ms = (float)pm_delta;
			}

			// Update the timestamp ONLY when the marcher finishes work
			pm_last_frame_tick = current_tick;
			changed = false;
		}
		if (Tbug){
			SDL_SetRenderDrawColor(renderer, 0, 0, 0, 128); 
			SDL_FRect rect2 = {8,8,screenWidth-16,52};
			SDL_RenderFillRect(renderer, &rect2);
			SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255); 
			#define offset screenWidth-strlen(debug_info)*8-8
			SDL_snprintf(debug_info, sizeof(debug_info), "P:tonemap ASDW:move QE: up down");
			SDL_RenderDebugText(renderer, offset , 10, debug_info);
			SDL_snprintf(debug_info, sizeof(debug_info), "|T| hide this");
			SDL_RenderDebugText(renderer, offset , 20, debug_info);
			SDL_snprintf(debug_info, sizeof(debug_info), "left click:grab objects, scroll: adjust grab distance");
			SDL_RenderDebugText(renderer, offset , 40, debug_info);
			SDL_snprintf(debug_info, sizeof(debug_info), "f2: screenshot");
			SDL_RenderDebugText(renderer,offset, 50, debug_info);
			SDL_snprintf(debug_info, sizeof(debug_info), "Render: %.1f ms", pm_render_ms);
			SDL_RenderDebugText(renderer, 10, 10, debug_info);
			SDL_snprintf(debug_info, sizeof(debug_info), "|9:-|0:+|bounce: %" PRIuFAST16, bounces);
			SDL_RenderDebugText(renderer, 10, 20, debug_info);
			SDL_snprintf(debug_info, sizeof(debug_info), "|-:-|+:+|samples: %" PRIuFAST16, samples);
			SDL_RenderDebugText(renderer, 10, 30, debug_info);
		}
	} else if (currentstate==enginestate::menu){
		if (currentstate!=laststate){
			selectcpp=0;
		}
		// Clear the screen with the color set above
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		SDL_RenderClear(renderer);
	}
	SDL_RenderPresent(renderer);
	SDL_LockMutex(render_mutex);
	SDL_UnlockMutex(render_mutex);
	laststate = currentstate;
	return SDL_APP_CONTINUE;
}
void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    SDL_Log("\n AppQuitting");
	running = false;
	SDL_UnlockTexture(framebuffer);
	SDL_UnlockMutex(render_mutex);
	if (thread) {
		SDL_WaitThread(thread, &renderthreadreturnvalue);
		SDL_Log("Thread returned value: %d", renderthreadreturnvalue);
	}
	if (render_mutex) SDL_DestroyMutex(render_mutex);
	render_mutex = nullptr;
	if (framebuffer) SDL_DestroyTexture(framebuffer);
	if (renderer) SDL_DestroyRenderer(renderer);
	if (window) SDL_DestroyWindow(window);
	SDL_Quit();
}
