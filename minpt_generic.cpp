#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <omp.h>
#include <optional>
#include <queue>
#include <random>
#include <thread>
#include <unordered_map>
#include <vector>
#include <cstdlib>
#include <type_traits>
#include <variant>
#include <tuple>
#include <cassert>
#include <functional>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

// ----------------------------------------------------------------------------

#define UNREACHABLE() assert(0)
#define UNREACHABLE_RETURN() assert(0); return {}

// ----------------------------------------------------------------------------

// Platform-dependent settings
#ifdef _MSC_VER
// Reverses byte order
int bswap(int x) { return _byteswap_ulong(x); }
// Sanitizes directory separator
std::string sanitizeSeparator(std::string p) { return p; }
#elif defined(__GNUC__)
#error TODO
#define NORETURN __attribute__((noreturn))
int bswap(int x) { return __builtin_bswap32(x); }
std::string pp(std::string p) {
    replace(p.begin(), p.end(), '\\', '/');
    return p;
}
#endif

// ----------------------------------------------------------------------------

// Type aliases and constants
namespace constant {
template <typename F> constexpr F Inf = F(1e+10);
template <typename F> constexpr F Eps = F(1e-4);
template <typename F> constexpr F Pi  = F(3.14159265358979323846);
}

#define USE_SIMPLIFIED_CONSTANT(F, Name) \
    static constexpr F Name = constant::Name<F>

#define USE_MATH_CONSTANTS(F) \
    USE_SIMPLIFIED_CONSTANT(F, Inf); \
    USE_SIMPLIFIED_CONSTANT(F, Eps); \
    USE_SIMPLIFIED_CONSTANT(F, Pi)

// ----------------------------------------------------------------------------

// Useful functions 
template <typename F>
F sq(F v) { return v * v; }

// ----------------------------------------------------------------------------

// 3d vector
// F: Floating point number type
template <typename F>
struct V {
    F x, y, z;
    V(F v = 0) : V(v, v, v) {}
    V(F x, F y, F z) : x(x), y(y), z(z) {}

    // Operators
    F operator[](int i) const { return (&x)[i]; }
    friend V operator+(V a, V b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
    friend V operator-(V a, V b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
    friend V operator*(V a, V b) { return { a.x * b.x, a.y * b.y, a.z * b.z }; }
    friend V operator/(V a, V b) { return { a.x / b.x, a.y / b.y, a.z / b.z }; }
    friend V operator-(V v) { return { -v.x, -v.y, -v.z }; }

    // Maximum element
    F m() const { return std::max({ x, y, z }); }

    // Element-wise min
    friend V vmin(V a, V b) {
        return { std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z) };
    }

    // Element-wise max
    friend V vmax(V a, V b) {
        return { std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z) };
    }

    // Dot product
    friend F dot(V a, V b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    // Normalize
    friend V norm(V v) {
        return v / std::sqrt(dot(v, v));
    }

    // Reflected direction
    friend V refl(V w, V n) {
        return F(2) * dot(w, n) * n - w;
    }

    // Refracted direction
    friend std::optional<V> refr(V wi, V n, F et) {
        const F t = dot(wi, n);
        const F t2 = F(1) - et * et * (F(1) - t * t);
        return t2 > 0 ? et * (n * t - wi) - n * sqrt(t2) : std::optional<V>{};
    }

    // Interpolation on barycentric coordinates
    friend V intp(V a, V b, V c, F u, F v) {
        return a * (F(1) - u - v) + b * u + c * v;
    }

    // Cross product
    friend V cross(V a, V b) {
        return { a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x };
    }

    // Orthogonal basis computation [Duff et al. 2017]
    friend std::tuple<V, V> odr(V n) {
        const F s = copysign(F(1), n.z);
        const F a = -F(1) / (s + n.z);
        const F b = n.x * n.y * a;
        const V u(1 + s * n.x * n.x * a, s * b, -s * n.x);
        const V v(b, s + n.y * n.y * a, -n.y);
        return { u, v };
    }
};

// ----------------------------------------------------------------------------

// Sampling-related
// Random number generator
template <typename F>
struct Rng {
    USE_MATH_CONSTANTS(F);

    std::mt19937 eng;
    std::uniform_real_distribution<F> dist;

    Rng(){};
    Rng(int seed) {
        eng.seed(seed);
        dist.reset();
    }

    // Sample unifom random number in [0,1)
    F u() { return dist(eng); }

    // Cosine-weighted direction sampling
    V<F> uD() {
        F r = sqrt(u()), t = F(2) * Pi * u();
        F x = r * cos(t), y = r * sin(t);
        return V(x, y, std::sqrt(std::max(F(0), F(1) - x * x - y * y)));
    }
};

// ----------------------------------------------------------------------------

// 1d discrete distribution
template <typename F>
struct Dist {
    std::vector<F> c{ F(0) }; // CDF

    // Add a value to the distribution
    void add(F v) {
        c.push_back(c.back() + v);
    }

    // Normalize the distribution
    void norm() {
        F sum = c.back();
        for (F& v : c) {
            v /= sum;
        }
    }

    // Evaluate pmf
    F p(int i) const {
        return (i < 0 || i + 1 >= int(c.size())) ? 0 : c[i + 1] - c[i];
    }

    // Sample from the distribution
    int samp(Rng<F>& rn) const {
        const auto it = std::upper_bound(c.begin(), c.end(), rn.u());
        return std::clamp(int(std::distance(c.begin(), it)) - 1, 0, int(c.size()) - 2);
    }
};

// ----------------------------------------------------------------------------

// 2d discrete distribution
template <typename F>
struct Dist2 {
    std::vector<Dist<F>> ds;   // Conditional distribution correspoinding to a row
    Dist<F> m;                 // Marginal distribution
    int w, h;                  // Size of the distribution

    // Add values to the distribution
    void init(const std::vector<F>& v, int a, int b) {
        w = a;
        h = b;
        ds.assign(h, {});
        for (int i = 0; i < h; i++) {
            auto& d = ds[i];
            for (int j = 0; j < w; j++) {
                d.add(v[i * w + j]);
            }
            m.add(d.c.back());
            d.norm();
        }
        m.norm();
    }

    // Evaluate pmf
    F p(F u, F v) const {
        const int y = std::min(int(v * h), h - 1);
        return m.p(y) * ds[y].p(int(u * w)) * w * h;
    }

    // Sample from the distribution
    std::tuple<F, F> samp(Rng<F>& rn) const {
        const int y = m.samp(rn);
        const int x = ds[y].samp(rn);
        return {(x + rn.u()) / w, (y + rn.u()) / h};
    }
};

// ----------------------------------------------------------------------------

#define USE_SIMPLIFIED_TYPE(F, Type) \
    using Type = Type<F>

#define USE_MATH_TYPES(F) \
    USE_MATH_CONSTANTS(F); \
    USE_SIMPLIFIED_TYPE(F, V); \
    USE_SIMPLIFIED_TYPE(F, Rng); \
    USE_SIMPLIFIED_TYPE(F, Dist); \
    USE_SIMPLIFIED_TYPE(F, Dist2)

// ----------------------------------------------------------------------------

// Surface geometry
// Set of vertex data representing scene geometry
template <typename F>
struct Geo {
    std::vector<V<F>> ps;   // Positions
    std::vector<V<F>> ns;   // Normals
    std::vector<V<F>> ts;   // Texture coordinates
};

// Ray
template <typename F>
struct Ray {
    V<F> o;            // Origin
    V<F> d;            // Direction
};

// Surface point
template <typename F>
struct Surf {
    USE_MATH_TYPES(F);

    V p;            // Position
    V n;            // Normal
    V t;            // Texture coordinates
    V u, v;         // Orthogonal tangent vectors

    Surf() {}
    Surf(V p, V n, V t) : p(p), n(n), t(t) {
        std::tie(u, v) = odr(n);
    }

    // Returns true if wi and wo is same direction according to the normal n
    bool op(V wi, V wo) const {
        return dot(wi, n) * dot(wo, n) <= 0;
    }

    // Returns orthonormal basis according to the incident direction wi
    std::tuple<V, V, V> obn(V wi) const {
        const int i = dot(wi, n) > 0;
        return {i ? n : -n, u, i ? v : -v};
    }

    // Geometry term
    friend F gt(const Surf& s1, const Surf& s2) {
        V d = s2.p - s1.p;
        const F L2 = dot(d, d);
        d = d / sqrt(L2);
        return abs(dot(s1.n, d)) * abs(dot(s2.n, -d)) / L2;
    }
};

// ----------------------------------------------------------------------------

// 2d texture
template <typename F>
struct Tex {
    USE_MATH_TYPES(F);

    std::string name;
    int w;               // Width of the texture
    int h;               // Height of the texture
    std::vector<F> cs;   // Colors
    std::vector<F> as;   // Alphas
    
    Tex() = default;

    // Calculate pixel coordinate of the vertically-flipped image
    int fl(int i) {
        const int j = i / 3;
        const int x = j % w;
        const int y = j / w;
        return 3 * ((h - y - 1) * w + x) + i % 3;
    }

    // Post procses a pixel for pmp textures
    F pf(int i, F e, std::vector<uint8_t>& ct) {
        // Gamma expansion
        return pow(F(ct[fl(i)]) / e, F(2.2));
    }

    // Post procses a pixel for pmf textures
    F pf(int i, F e, std::vector<float>& ct) {
        if (e < 0) {
            return ct[fl(i)];
        }
        auto m = bswap(*(int32_t *)&ct[fl(i)]);
        return *(float *)&m;
    }

    // Load a ppm or a pfm texture
    template <typename T>
    void loadpxm(std::vector<F>& c, std::string p) {
        std::cout << "Loading texture: " << p << std::endl;
        static std::vector<T> ct;
        FILE *f = std::fopen(p.c_str(), "rb");
        if (!f) {
            return;
        }
        double e;
        std::fscanf(f, "%*s %d %d %lf%*c", &w, &h, &e);
        const int sz = w * h * 3;
        ct.assign(sz, 0);
        c.assign(sz, 0);
        std::fread(ct.data(), sizeof(T), sz, f);
        for (int i = 0; i < sz; i++) {
            c[i] = pf(i, F(e), ct);
        }
        std::fclose(f);
    }

    // Load pfm texture
    void loadpfm(std::string p) {
        loadpxm<float>(cs, p);
    }

    // Load ppm texture
    void load(std::string p) {
        auto b = std::filesystem::path(p);
        auto pc = b.replace_extension(".ppm").string();
        auto pa = (b.parent_path() / std::filesystem::path(b.stem().string() + "_alpha.ppm")).string();
        loadpxm<uint8_t>(cs, pc);
        loadpxm<uint8_t>(as, pa);
    }

    // Load png texture
    void loadpng(const std::string& p) {
        std::cout << "Loading texture: " << p << std::endl;
        //int width = 0;
        //int height = 0;
        int channels = 0;
        if (stbi_is_hdr(p.c_str())) {
            float* pixels = stbi_loadf(p.c_str(), &w, &h, &channels, 0);
        }
        else {
            unsigned char* pixels = stbi_load(p.c_str(), &w, &h, &channels, 4);
            const int sz = w * h;
            cs.assign(sz * 3, 0);
            if (channels > 3) as.assign(sz, 0);
            for (int i = 0; i < sz; ++i) {
                for (int c = 0; c <= 2; ++c) {
                    cs[i * 3 + c] = float(pixels[i * channels + c]) / 255.0;
                }
                if (channels > 3) as[i] = float(pixels[i * channels + 3]) / 255.0;
                //else as[i] = 1.0;
            }
            stbi_image_free(pixels);
        }
    }

    // Evaluate the texture on the given pixel coordinate
    V ev(V t, bool alpha = false) const {
        const F u = t.x - floor(t.x);
        const F v = t.y - floor(t.y);
        const int x = std::clamp(int(u * w), 0, w - 1);
        const int y = std::clamp(int(v * h), 0, h - 1);
        const int i = w * y + x;
        return V(cs[3 * i], cs[3 * i + 1], cs[3 * i + 2]);
    }

    F evAlpha(V t) const {
        const F u = t.x - floor(t.x);
        const F v = t.y - floor(t.y);
        const int x = std::clamp(int(u * w), 0, w - 1);
        const int y = std::clamp(int(v * h), 0, h - 1);
        const int i = w * y + x;
        //return as[3 * i];
        return as[i];
    }
};

// ----------------------------------------------------------------------------

// Axis-aligned bounding box
template <typename F>
struct B {
    USE_MATH_TYPES(F);

    V mi = V(Inf);
    V ma = V(-Inf);
    V operator[](int i) const { return (&mi)[i]; }

    // Centroid of the bound
    V center() const { return (mi + ma) * .5; }

    // Surface area of the bound
    F sa() const {
        V d = ma - mi;
        return F(2) * (d.x * d.y + d.y * d.z + d.z * d.x);
    }

    // Check intersection to the ray
    // http://psgraphics.blogspot.de/2016/02/new-simple-ray-box-test-from-andrew.html
    bool isect(const Ray<F>& r, F tl, F th) const {
        for (int i = 0; i < 3; i++) {
            const F vd = F(1) / r.d[i];
            F t1 = (mi[i] - r.o[i]) * vd;
            F t2 = (ma[i] - r.o[i]) * vd;
            if (vd < 0) {
                std::swap(t1, t2);
            }
            tl = std::max(t1, tl);
            th = std::min(t2, th);
            if (th < tl) {
                return false;
            }
        }
        return true;
    }

    // Merges a bound and a point
    friend B merge(B b, V p) {
        return { vmin(b.mi, p), vmax(b.ma, p) };
    }

    // Merges two bounds
    friend B merge(B a, B b) {
        return { vmin(a.mi, b.mi), vmax(a.ma, b.ma) };
    }
};

// ----------------------------------------------------------------------------

#define USE_RENDER_PRIMITIVE_TYPE(F) \
    USE_MATH_TYPES(F); \
    USE_SIMPLIFIED_TYPE(F, Geo); \
    USE_SIMPLIFIED_TYPE(F, Ray); \
    USE_SIMPLIFIED_TYPE(F, Surf); \
    USE_SIMPLIFIED_TYPE(F, Tex); \
    USE_SIMPLIFIED_TYPE(F, B);

// ----------------------------------------------------------------------------

// Indices to vertex information
struct Ind {
    int p = -1;   // Index to position
    int t = -1;   // Index to texture coordinates
    int n = -1;   // Index to normal
};

// Sample result
template <typename F>
struct Sample {
    Ray<F> ray;
    V<F> weight;
};
template <typename F>
struct LightSample {
    V<F> wo;    // Sampled direction
    F d;        // Distance to the sampled position
    V<F> fs;    // Evaluated Le
    F p;        // Evaluated probablity
};

// Forward declaration
template <typename F> class Obj;

// ----------------------------------------------------------------------------

// Area light
template <typename F>
class AreaL {
private:
    USE_RENDER_PRIMITIVE_TYPE(F);

private:
    V Ke_;                  // Luminance
    Dist dist_;             // For surface sampling of area lights
    F invA_;                // Inverse area of area lights
    const Geo& geo_;        // Refence of scene geometry
    std::vector<Ind> fs_;   // Face indices

public:
    AreaL(V Ke, const Geo& geo, const std::vector<Ind>& fs) : Ke_(Ke), geo_(geo), fs_(fs) {
        for (size_t fi = 0; fi < fs.size(); fi += 3) {
            const V a = geo.ps[fs[fi].p];
            const V b = geo.ps[fs[fi + 1].p];
            const V c = geo.ps[fs[fi + 2].p];
            const V cr = cross(b - a, c - a);
            dist_.add(sqrt(dot(cr, cr)) * F(.5));
        }
        invA_ = F(1) / dist_.c.back();
        dist_.norm();
    }

    std::optional<Sample<F>> samp(Rng& rn, const Surf& sp, const V& wi) const {
        throw std::runtime_error("not implemented");
        return {};
    }

    std::optional<LightSample<F>> sampL(Rng& rn, const Surf& sp) const {
        const int i = dist_.samp(rn);
        const F s = std::sqrt(std::max(F(0), rn.u()));
        const V a = geo_.ps[fs_[3 * i].p];
        const V b = geo_.ps[fs_[3 * i + 1].p];
        const V c = geo_.ps[fs_[3 * i + 2].p];
        const Surf spL(intp(a, b, c, 1 - s, rn.u() * s), norm(cross(b - a, c - a)), {});
        const V pp = spL.p - sp.p;
        const V wo = norm(pp);
        const F p = pdfL(sp, spL, -wo);
        if (p == F(0)) {
            return {};
        }
        return LightSample<F>{ wo, sqrt(dot(pp, pp)), ev(spL, {}, -wo), p };
    }

    F pdfL(const Surf& sp, const Surf& spL, const V& wo) const {
        F G = gt(sp, spL);
        return G == F(0) ? F(0) : invA_ / G;
    }

    V ev(const Surf& sp, const V& wi, const V& wo) const {
        return dot(wo, sp.n) <= F(0) ? V() : Ke_;
    }
};

// Environment light
template <typename F>
class EnvL {
private:
    USE_RENDER_PRIMITIVE_TYPE(F);

private:
    Tex map_;       // Environment map
    F rot_;         // Rotation of the environment map around (0,1,0)
    Dist2 dist_;    // For samplign directions

public:
    struct Params {
        std::string env;    // Path to .pfm file for environment map 
        F rot;              // Rotation angle of the environment map
    };
    EnvL(const Params& params) {
        map_.loadpfm(params.env);
        rot_ = params.rot * Pi / F(180);
        auto& cs = map_.cs;
        const int w = map_.w;
        const int h = map_.h;
        std::vector<F> ls(w * h);
        for (int i = 0; i < w * h; i++) {
            V v(cs[3*i], cs[3*i+1], cs[3*i+2]);
            ls[i] = v.m() * sin(Pi * (F(i)/w + F(.5)) / h);
        }
        dist_.init(ls, w, h);
    }

    std::optional<Sample<F>> samp(Rng& rn, const Surf& sp, const V& wi) const {
        throw std::runtime_error("not implemented");
        return {};
    }

    std::optional<LightSample<F>> sampL(Rng& rn, const Surf& sp) const {
        auto[u, v] = dist_.samp(rn);
        F t = Pi * v, st = sin(t);
        F p = 2 * Pi * u + rot_;
        V wo(st * sin(p), cos(t), st * cos(p));
        F pL = pdfL(sp, {}, -wo);
        if (pL == F(0)) {
            return {};
        }
        return LightSample<F>{ wo, Inf, ev({}, {}, -wo), pL };
    }

    F pdfL(const Surf& sp, const Surf& spL, const V& wo) const {
        V d = -wo;
        F at = atan2(d.x, d.z);
        at = at < F(0) ? at + F(2) * Pi : at;
        F t = (at - rot_) * F(.5) / Pi;
        F u = t - floor(t);
        F v = acos(d.y) / Pi;
        F st = sqrt(1 - d.y * d.y);
        return st == F(0) ? F(0) : dist_.p(u, v) / (F(2) * Pi*Pi*st*abs(dot(-wo, sp.n)));
    }

    V ev(const Surf& sp, const V& wi, const V& wo) const {
        const V d = -wo;
        F at = atan2(d.x, d.z);
        at = at < F(0) ? at + F(2) * Pi : at;
        F t = (at - rot_) * F(.5) / Pi;
        return map_.ev({ t - floor(t), acos(d.y) / Pi, F(0) });
    }
};

// Pinhole camera
template <typename F>
class PinholeE {
private:
    USE_RENDER_PRIMITIVE_TYPE(F);

private:
    V position_;    // Sensor position
    V u_, v_, w_;   // Basis for camera coordinates
    F tf_;          // Target focus distance
    F aspect_;      // Aspect ratio

public:
    struct Params {
        using Base = PinholeE<F>;
        V e;        // Camera position
        V c;        // Look-at position
        V u;        // Up vector
        F fv;       // Vertical FoV
        F aspect;   // Aspect ratio
    };
    PinholeE(const Params& params) {
        position_ = params.e;
        tf_ = tan(params.fv * Pi / F(180) * F(.5));
        aspect_ = params.aspect;
        w_ = norm(params.e - params.c);
        u_ = norm(cross(params.u, w_));
        v_ = cross(w_, u_);
    }

    std::optional<Sample<F>> samp(Rng& rn, const Surf& sp, const V& wi) const {
        const V rp = 2 * wi - 1;
        // Direction in sensor coodinates
        const V d = -norm(V(aspect_ * tf_ * rp.x, tf_ * rp.y, F(1)));
        return Sample<F>{ Ray{ position_, u_*d.x+v_*d.y+w_*d.z }, V(1) };
    }
};

// Realistic camera
template <typename F>
class RealisticE {
private:
    USE_RENDER_PRIMITIVE_TYPE(F);

private:
    // Lens element
    // See Fig.1 of [Kolb et al. 1995] for detail.
    struct Lens {
        F cr;           // Curvature radius
        F t;            // Thickness
        F e;            // Indef of refraction
        F ar;           // Aperture radius
    };

    V position_;        // Sensor position
    V u_, v_, w_;       // Basis for camera coordinates
    F tf_;              // Target focus distance
    F aspect_;          // Aspect ratio
    F sensorSize_;      // Diagonal length of the sensor
    F sensitivity_;     // Sensitivity of the sensor

    // Calculated distance from the sensor to the nearest lens element
    F distanceToNearestLensElement_;
    // Lens elements added from the object side
    std::vector<Lens> lens_;
    // Bounds of exit pupils measured from several positions in the image plane
    std::vector<std::optional<B>> exitPopilBounds_;

public:
    struct Params {
        using Base = RealisticE<F>;
        std::string lensFile;   // Path to lens file
        V e, c, u;              // Camera position, look-at position, up vector
        F fv;                   // Vertical FoV
        F fd;                   // Focus distance
        F sensorSize;           // Diagonal length of the sensor
        F sensitivity;          // Sensitivity
        F aspect;               // Aspect ratio
    };
    RealisticE(const Params& params) {
        // Assign parameters
        position_ = params.e;
        tf_ = tan(params.fv * Pi / F(180) * F(.5));
        aspect_ = params.aspect;
        sensitivity_ = params.sensitivity;
        sensorSize_ = params.sensorSize * F(.001);
        w_  = norm(params.e - params.c);
        u_  = norm(cross(params.u, w_));
        v_  = cross(w_, u_);

        // Loads lens system data
        char l[4096];
        std::ifstream f(params.lensFile);
        std::cout << "Loading lens file: " << params.lensFile << std::endl;
        while (f.getline(l, 4096)) {
            if (l[0] == '#' || l[0] == '\0') {
                continue;
            }
            double cr, t, eta, ar;
            sscanf(l, "%lf %lf %lf %lf", &cr, &t, &eta, &ar);
            lens_.push_back({F(cr * .001), F(t * .001), F(eta), F(ar * .001 * .5)});
        }
        if (lens_.empty()) {
            throw std::runtime_error("Invalid lens file");
        }

        // Autofocus
        distanceToNearestLensElement_ = [&]() {
            F lo = Eps, hi = Inf;
            for (int i = 0; i < 99; i++) {
                F mi = (lo + hi) * F(.5);
                (ffd(mi) < params.fd ? hi : lo) = mi;
            }
            return hi;
        }();

        // Precompute exit pupils
        // Computes exit pupils for several positions in the image plane.
        // It is enough to check one axis perpendicular to the optical axis
        // because the lens system is symmetric aroudn the optical axis.
        Rng rn(42);
        int n = 64;
        exitPopilBounds_.assign(n, {});
        F cfv = -1;
        F sy = sqrt(sensorSize_ * sensorSize_ / (1 + aspect_ * aspect_));
        F sx = aspect_ * sy;
        int m = 1 << 12;
        for (int i = 0; i < n; i++) {
            B b;
            bool f = 0;
            auto& lb = lens_.back();
            V p(0, 0, -lb.t + distanceToNearestLensElement_);
            for (int j = 0; j < m; j++) {
                p.x = (i + rn.u()) / n * sensorSize_ * F(.5);
                const F r = sqrt(rn.u());
                const F t = F(2) * Pi * rn.u();
                const V pl(r * cos(t) * lb.ar, r * sin(t) * lb.ar, -lb.t);
                const auto rt = trl({p, norm(pl - p)});
                if (rt) {
                    f = 1;
                    b = merge(b, pl);
                    if (p.x < sx * .5) {
                        cfv = std::max(cfv, rt->d.z);
                    }
                }
            }
            if (f) {
                exitPopilBounds_[i] = b;
            }
        }
        // Prints effective vertical field of view
        printf("Effective vertical FoV: %lf\n",
            std::atan(std::tan(Pi - std::acos(cfv)) / aspect_) * F(180) / Pi * F(2));
    }

    std::optional<Sample<F>> samp(Rng& rn, const Surf& sp, const V& wi) const {
        const V rp = 2 * wi - 1;
        const int n = int(exitPopilBounds_.size());
        const auto &lb = lens_.back();
        
        // Determine a position on the sensor plane
        const F sy = std::sqrt(sensorSize_ * sensorSize_ / (F(1) + aspect_ * aspect_));
        const V o = rp * V(aspect_*sy*F(.5), sy*F(.5), 0) + V(F(0), F(0), distanceToNearestLensElement_);

        // Selects a bound of the exit pupil corresponding to the pixel position
        const F l = std::sqrt(o.x * o.x + o.y * o.y);
        const int i = std::clamp(int(l / sensorSize_ * 2 * n), 0, n - 1);
        const auto &b = exitPopilBounds_[i];
        if (!b) {
            return {};
        }

        // Sample a position on the exit pupil and calculate the initial ray direction
        const V bl = b->ma - b->mi;
        const V p = b->mi + bl * V(rn.u(), rn.u(), 0);
        const F s = l != 0 ? o.y / l : 0, c = l != 0 ? o.x / l : 1;
        const V d = norm(V(c*p.x - s*p.y, s*p.x + c*p.y, p.z) - o);

        // Trace rays through the lens system
        const auto r = trl({ o, d });
        if (!r) {
            return {};
        }

        // Calculate contribution
        const F A = bl.x * bl.y;
        const F Z = lb.t + distanceToNearestLensElement_;
        const F w = d.z * d.z * d.z * d.z * A / (Z * Z);

        return Sample<F>{ Ray{ u_*r->o.x+v_*r->o.y+w_*r->o.z+position_,
            u_*r->d.x+v_*r->d.y+w_*r->d.z }, V(w*sensitivity_) };
    }

private:
    // Traces the lens system. Returns outgoing ray passing through the lens system.
    // Returns nullopt if it failed.
    std::optional<Ray> trl(Ray r) const {
        F z = 0;
        for (int i = int(lens_.size()) - 1; i >= 0; i--) {
            const auto& l = lens_[i];
            z -= l.t;

            // Intersection with lens element
            struct LH {     // ----- Lens hit information
                F t;        // Distance in the direction of the current ray
                V n;        // Normal at the hit point
            };
            auto h = [&]() -> std::optional<LH> {
                // Case: aperture stop
                if (l.cr == 0) {
                    // Check intersection w/ aperture stop
                    F t = (z - r.o.z) / r.d.z;
                    return t < 0 ? std::optional<LH>{} : LH{t, {}};
                }

                // Case: spherical lens element
                // Check intersection w/ spherical lens element
                const V c(0, 0, z + l.cr);
                const V oc = c - r.o;
                const F b = dot(oc, r.d);
                const F dt = b * b - dot(oc, oc) + l.cr * l.cr;
                if (dt < 0) {
                    // No intersection
                    return {};
                }
                const F t0 = b - sqrt(dt);
                const F t1 = b + sqrt(dt);
                const F t = (r.d.z > 0) ^ (l.cr < 0)
                    ? std::min(t0, t1) : std::max(t0, t1);
                if (t < 0) {
                    // No intersection in positive direction
                    return {};
                }
                V n = (r.o + t * r.d - c) / l.cr;
                n = dot(n, -r.d) < 0 ? -n : n;
                return LH{t, n};
            }();
            if (!h) {
                return {};
            }

            // Intersection with apearture
            const V p = r.o + r.d * h->t;
            if (p.x * p.x + p.y * p.y > l.ar * l.ar) {
                return {};
            }

            // Setup next ray
            // Case: aperture stop
            r.o = p;
            if (l.cr == 0) {
                // Use the same direction
                continue;
            }

            // Case: spherical lens element
            // Calculates the refracted direction
            const F et = l.e / (i > 0 && lens_[i-1].e != 0 ? lens_[i-1].e : 1);
            const auto wt = refr(-r.d, h->n, et);
            if (!wt) {
                // Total internal reflection
                return {};
            }
            r.d = *wt;
        }
        return r;
    }

    // Computes effective focus distance given the distance between
    // the last lens element and the image plane.
    F ffd(F id) const {
        std::optional<Ray> r;
        const auto& lb = lens_.back();
        // Trace several rays parallel to the optical axis
        for (int i = 9; i > 0; i--) {
            if (r = trl({V(0,0,-lb.t+id), norm(V(lb.ar*i/10,0,-id))})) {
                break;
            }
        }
        if (!r) {
            return Inf;
        }
        const F t = -r->o.x / r->d.x;
        const F z = (r->o + t * r->d).z;
        F sz = 0;
        for (const auto& l : lens_) {
            sz += l.t;
        }
        // The value is valid if the ray is intersected with
        // optical axis before the initial lens element.
        return z < sz ? -z : Inf;
    }
};

// Diffuse material
template <typename F>
class D {
private:
    USE_RENDER_PRIMITIVE_TYPE(F);
    friend class Obj<F>;

private:
    V Kd_;
    const Tex* mapKd_ = nullptr;

public:
    D(V Kd, const Tex* mapKd) : Kd_(Kd), mapKd_(mapKd) {}

    std::optional<Sample<F>> samp(Rng& rn, const Surf& sp, const V& wi) const {
        auto[n, u, v] = sp.obn(wi);
        const V Kd = mapKd_ ? mapKd_->ev(sp.t) : Kd_;
        const V d = rn.uD();
        return Sample<F>{ { sp.p, u*d.x+v*d.y+n*d.z }, Kd };
    }

    F pdf(const Surf& sp, const V& wi, const V& wo) const {
        return sp.op(wi, wo) ? F(0) : F(1) / Pi;
    }

    V ev(const Surf& sp, const V& wi, const V& wo) const {
        if (sp.op(wi, wo)) {
            return {};
        }
        const F a = (mapKd_ && !mapKd_->as.empty()) ? mapKd_->evAlpha(sp.t) : F(1);
        return (mapKd_ ? mapKd_->ev(sp.t) : Kd_) * (a / Pi);
    }
};

// Glossy material
template <typename F>
class G {
private:
    USE_RENDER_PRIMITIVE_TYPE(F);
    friend class Obj<F>;

private:
    // Specular reflectance, roughness
    V Ks_;
    F ax_, ay_;

public:
    G(V Ks, F Ns, F an) : Ks_(Ks) {    
        F r = F(2) / (F(2) + Ns);
        F as = std::sqrt(F(1) - an * F(.9));
        ax_ = std::max(F(1e-3), r / as);
        ay_ = std::max(F(1e-3), r * as);
    }

    std::optional<Sample<F>> samp(Rng& rn, const Surf& sp, const V& wi) const {
        const auto[n, u, v] = sp.obn(wi);
        const F u1 = rn.u() * 2 * Pi;
        const F u2 = rn.u();
        const V wh = norm(sqrt(u2 / (F(1) - u2))*(ax_*cos(u1)*u + ay_*sin(u1)*v) + n);
        const V wo = refl(wi, wh);
        if (sp.op(wi, wo)) {
            return {};
        }
        return Sample<F>{ { sp.p, wo }, ev(sp, wi, wo) / pdf(sp, wi, wo) };
    }

    F pdf(const Surf& sp, const V& wi, const V& wo) const {
        if (sp.op(wi, wo)) {
            return 0;
        }
        const V wh = norm(wi + wo);
        const auto[n, u, v] = sp.obn(wi);
        return GGX_D(wh, u, v, n) * dot(wh, n) / (F(4) * dot(wo, wh) * dot(wo, n));
    }

    V ev(const Surf& sp, const V& wi, const V& wo) const {
        if (sp.op(wi, wo)) {
            return {};
        }
        const V wh = norm(wi + wo);
        const auto[n, u, v] = sp.obn(wi);
        const V Fr = Ks_ + (F(1) - Ks_) * std::pow(F(1) - dot(wo, wh), F(5));
        return Ks_ * Fr * (GGX_D(wh, u, v, n) * GGX_G(wi, wo, u, v, n) / (F(4) * dot(wi, n) * dot(wo, n)));
    }

private:
    // Normal distribution of anisotropic GGX
    F GGX_D(V wh, V u, V v, V n) const {
        return F(1) / (Pi*ax_*ay_*sq(sq(dot(wh, u) / ax_) + sq(dot(wh, v) / ay_) + sq(dot(wh, n))));
    }

    // Smith's G term correspoinding to the anisotropic GGX
    F GGX_G(V wi, V wo, V u, V v, V n) const {
        auto G1 = [&](V w) {
            const F c = dot(w, n);
            const F s = sqrt(1 - c * c);
            const F cp = dot(w, u) / s;
            const F cs = dot(w, v) / s;
            const F a2 = sq(cp * ax_) + sq(cs * ay_);
            return c == 0 ? 0 : 2 / (1 + sqrt(1 + a2 * sq(s / c)));
        };
        return G1(wi) * G1(wo);
    }
};

// Transparent mask
template <typename F>
class M {
private:
    USE_RENDER_PRIMITIVE_TYPE(F);
   
public:
    M() = default;
    std::optional<Sample<F>> samp(Rng& rn, const Surf& sp, const V& wi) const {
        return Sample<F>{ Ray{ sp.p, -wi }, V(F(1)) };
    }
};

// Fresnel relfection/refraction
template <typename F>
class Fres {
private:
    USE_RENDER_PRIMITIVE_TYPE(F);

private:
    F Ni_;

public:
    Fres(F Ni) : Ni_(Ni) {}
    std::optional<Sample<F>> samp(Rng& rn, const Surf& sp, const V& wi) const {
        bool i = dot(wi, sp.n) > F(0);
        V n = i ? sp.n : -sp.n;
        F et = i ? F(1) / Ni_ : Ni_;
        auto wt = refr(wi, n, et);
        F Fr = !wt ? 1 : [&]() {
            // Flesnel term
            F cos = i ? dot(wi, sp.n) : dot(*wt, sp.n);
            F r = (F(1) - Ni_) / (F(1) + Ni_);
            r = r * r;
            return r + (F(1) - r) * std::pow(F(1) - cos, F(5));
        }();
        if (rn.u() < Fr) {
            // Reflection
            return Sample<F>{ Ray{ sp.p, refl(wi, sp.n) }, V(F(1)) };
        }
        // Transmission
        return Sample<F>{ Ray{ sp.p, *wt }, V(et * et) };
    }
};

// Perfect mirror reflection
template <typename F>
class PRefl {
private:
    USE_RENDER_PRIMITIVE_TYPE(F);

public:
    PRefl() = default;
    std::optional<Sample<F>> samp(Rng& rn, const Surf& sp, const V& wi) const {
        return Sample<F>{ Ray{ sp.p, refl(wi, sp.n)} , V(F(1)) };
    }
};

// ----------------------------------------------------------------------------

// Type of initialization parameters
template <typename CompT>
using Params = typename CompT::Params;

// Helper for std::visit
// https://en.cppreference.com/w/cpp/utility/variant/visit
template<typename... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<typename... Ts> overloaded(Ts...) -> overloaded<Ts...>;

// Helper for decorating std::variant with reference types
template <typename>
struct ref_decorator_;
template <typename... Ts>
struct ref_decorator_<std::variant<Ts...>> {
    using type = std::variant<std::reference_wrapper<Ts>...>;
};
template <typename V>
using ref_decorator_t = typename ref_decorator_<V>::type;

// Helper for decorating std::variant with const reference types
template <typename>
struct const_ref_decorator_;
template <typename... Ts>
struct const_ref_decorator_<std::variant<Ts...>> {
    using type = std::variant<std::reference_wrapper<const Ts>...>;
};
template <typename V>
using const_ref_decorator_t = typename const_ref_decorator_<V>::type;

// Get variant of references
template <typename VariantT>
ref_decorator_t<VariantT> variant_ref(VariantT& v) {
    return std::visit([](auto&& arg) -> ref_decorator_t<VariantT> {
        return arg;
    }, v);
}

// Get variant of const references
template <typename VariantT>
const_ref_decorator_t<VariantT> variant_cref(const VariantT& v) {
    return std::visit([](auto&& arg) -> const_ref_decorator_t<VariantT> {
        return arg;
    }, v);
}

// Type holder
template <typename... Ts> struct TypeHolder;

// Check if type is contained in the holder
template <typename, typename>
struct is_one_of_;
template <typename T, typename... Ts>
struct is_one_of_<T, TypeHolder<Ts...>> {
    static constexpr bool value() {
        return (std::is_same_v<T, Ts> || ...);
    }
};
template <typename T, typename HolderT>
constexpr bool is_one_of_v = is_one_of_<T, HolderT>::value();

// ----------------------------------------------------------------------------

// Variant type of surface interaction
template <typename F>
using ObjComp = std::variant<
    PinholeE<F>,
    RealisticE<F>,
    AreaL<F>,
    EnvL<F>,
    D<F>,
    G<F>,
    M<F>,
    Fres<F>,
    PRefl<F>>;
template <typename F>
using ObjCompRef = ref_decorator_t<ObjComp<F>>;
template <typename F>
using ObjCompConstRef = const_ref_decorator_t<ObjComp<F>>;

// Emitter types
template <typename F>
using Emitter = TypeHolder<
    PinholeE<F>,
    RealisticE<F>,
    AreaL<F>,
    EnvL<F>>;

// Light types
template <typename F>
using Light = TypeHolder<
    AreaL<F>,
    EnvL<F>>;

// Specular types
template <typename F>
using Specular = TypeHolder<
    Fres<F>,
    PRefl<F>>;

// Non-specular types
template <typename F>
using NonSpecular = TypeHolder<
    D<F>,
    G<F>>;

// ----------------------------------------------------------------------------

template <typename F> class Scene;

// Scene object
template <typename F>
class Obj {
private:
    USE_RENDER_PRIMITIVE_TYPE(F);
    friend class Scene<F>;

private:
    std::vector<Ind> fs;            // Face indices
    std::vector<ObjComp<F>> comps;  // Object components

public:
    Obj() = default;
    Obj(ObjComp<F>&& comp)
        : comps{ comp } {}
    template <typename... Ts>
    Obj(const std::vector<Ind>& fs, Ts&&... comp)
        : fs(fs), comps{ comp... } {}

public:
    // Component sampling. Enable endpoint if you are sampling from an emitter.
    template <typename Callable>
    bool sampComp(Rng& rn, const Surf& sp, bool endpoint, const V& wi, Callable&& process) const {
        using namespace std::placeholders;
        // Emitter type
        if (endpoint) {
            const auto comp = findComp<Emitter<F>>();
            if (!comp) {
                return false;
            }
            const auto vref = variant_cref(comp->get());
            return std::visit([&](const auto& vref) {
                return process(vref.get(), F(1));
            }, vref);
        }
        // Non-specular type
        if (contains<NonSpecular<F>>()) {
            // Select a component from D or G using RR
            const auto& compD = getComp<D<F>>();
            const auto& compG = getComp<G<F>>();
            F wd = (compD.mapKd_ ? compD.mapKd_->ev(sp.t) : compD.Kd_).m();
            F ws = compG.Ks_.m();
            if (wd == 0 && ws == 0) {
                wd = 1;
                ws = 0;
            }
            F s = wd + ws;
            wd /= s;
            ws /= s;
            if (rn.u() < wd) {
                // Transparency mask
                const bool useMask = compD.mapKd_ && !compD.mapKd_->as.empty();
                if (useMask && rn.u() > compD.mapKd_->evAlpha(sp.t)) {
                    M<F> compM;
                    return process(std::cref(compM).get(), wd);
                }
                return process(std::cref(compD).get(), wd);
            }
            return process(std::cref(compG).get(), ws);
        }
        // Specular types
        else {
            const auto comp = findComp<Specular<F>>();
            if (!comp) {
                return false;
            }
            const auto vref = variant_cref(comp->get());
            return std::visit([&](const auto& vref) {
                return process(vref.get(), F(1));
            }, vref);
        }
        UNREACHABLE_RETURN();
    }

    // Visit underlying component
    template <typename HolderT, typename Callable>
    bool visitComp(Callable&& process) const {
        const auto comp = findComp<HolderT>();
        if (!comp) {
            return false;
        }
        return std::visit([&](const auto& vref) -> bool {
            using CompT = std::decay_t<decltype(vref)>;
            if constexpr (!is_one_of_v<CompT, HolderT>) {
                return false;
            }
            else {
                return process(vref);
            }
            UNREACHABLE();
            return false;
        }, comp->get());
    }

public:
    // Find and get copmonent
    template <typename CompT>
    const CompT& getComp() const {
        const auto comp = findComp<TypeHolder<CompT>>();
        assert(comp);
        return std::get<CompT>(comp->get());
    }
    // Find component with matching type. Returns index of comps
    template <typename Ts>
    std::optional<std::reference_wrapper<const ObjComp<F>>> findComp() const {
        return find_helper_<Ts>::find(comps);
    }
    // Check object components contains specific types
    template <typename HolderT>
    bool contains() const {
        return find_helper_<HolderT>::contains(comps);
    }
    template <typename>
    struct find_helper_;
    template <typename... Ts>
    struct find_helper_<TypeHolder<Ts...>> {
        static std::optional<std::reference_wrapper<const ObjComp<F>>> find(const std::vector<ObjComp<F>>& comps) {
            for (const auto& comp : comps) {
                if ((std::holds_alternative<Ts>(comp) || ...)) {
                    return std::cref(comp);
                }
            }
            return {};
        }
        static bool contains(const std::vector<ObjComp<F>>& comps) {
            for (const auto& comp : comps) {
                if ((std::holds_alternative<Ts>(comp) || ...)) {
                    return true;
                }
            }
            return false;
        }
    };
};

// ----------------------------------------------------------------------------

// Wavefront OBJ file parser
// Basic material parameters (extracted from .mtl file)
template <typename F>
struct MatParams {
    USE_MATH_TYPES(F);
    int illum;               // Type
    V Kd;                    // Diffuse reflectance
    V Ks;                    // Specular reflectance
    V Ke;                    // Luminance
    Tex<F> *mapKd = nullptr; // Texture for Kd
    F Ni;                    // Index of refraction
    F Ns;                    // Specular exponent for phong shading
    F an;                    // Anisotropy
};

template <typename F>
class WavefrontOBJParser {
private:
    USE_MATH_TYPES(F);

private:
    // Material parameters
    std::vector<MatParams<F>> ms;
    std::unordered_map<std::string, int> msmap;  // Map of material name
    std::unordered_map<std::string, int> tsmap;  // Map of texture name

    // References
    Geo<F>& geo;
    std::vector<std::unique_ptr<Tex<F>>>& ts;

public:
    WavefrontOBJParser(Geo<F>& geo, std::vector<std::unique_ptr<Tex<F>>>& ts)
        : geo(geo), ts(ts) {}

    // Parses .obj file
    void parse(std::string p, const std::function<void(const MatParams<F>& m, const std::vector<Ind>& fs)>& processMesh) {
        char l[4096], name[256];
        std::ifstream f(p);
        std::cout << "Loading OBJ file: " << p << std::endl;

        // Active face indices and material index
        int currMaterialIdx = -1;
        std::vector<Ind> currfs;

        // Parse .obj file line by line
        while (f.getline(l, 4096)) {
            char *t = l;
            ss(t);
            if (cm(t, "v", 1)) {
                geo.ps.emplace_back(nv(t += 2));
            } else if (cm(t, "vn", 2)) {
                geo.ns.emplace_back(nv(t += 3));
            } else if (cm(t, "vt", 2)) {
                geo.ts.emplace_back(nv(t += 3));
            } else if (cm(t, "f", 1)) {
                t += 2;
                if (ms.empty()) {
                    ms.push_back({ -1, V(1) });  // NonS
                    currMaterialIdx = 0;
                }
                Ind is[4];
                for (auto& i : is) {
                    i = pind(t);
                }
                currfs.insert(currfs.end(), {is[0], is[1], is[2]});
                if (is[3].p != -1) {
                    currfs.insert(currfs.end(), {is[0], is[2], is[3]});
                }
            } else if (cm(t, "usemtl", 6)) {
                t += 7;
                nn(t, name);
                if (!currfs.empty()) {
                    processMesh(ms[currMaterialIdx], currfs);
                    currfs.clear();
                }
                currMaterialIdx = msmap.at(name);
            } else if (cm(t, "mtllib", 6)) {
                nn(t += 7, name);
                loadmtl(sanitizeSeparator((std::filesystem::path(p).remove_filename() / name).string()), ts);
            } else {
                continue;
            }
        }
        if (!currfs.empty()) {
            processMesh(ms[currMaterialIdx], currfs);
        }
    }

private:
    // Checks a character is space-like
    bool ws(char c) { return c == ' ' || c == '\t'; };

    // Checks the token is a command 
    bool cm(char *&t, const char *c, int n) { return !strncmp(t, c, n) && ws(t[n]); }

    // Skips spaces
    void ss(char *&t) { t += strspn(t, " \t"); }

    // Skips spaces or /
    void sc(char *&t) { t += strcspn(t, "/ \t"); }

    // Parses floating point value
    F nf(char *&t) {
        ss(t);
        F v = F(atof(t));
        sc(t);
        return v;
    }

    // Parses int value
    int ni(char *&t) {
        ss(t);
        int v = atoi(t);
        sc(t);
        return v;
    }

    // Parses 3d vector
    V nv(char *&t) {
        V v;
        v.x = nf(t);
        v.y = nf(t);
        v.z = nf(t);
        return v;
    }

    // Parses vertex index. See specification of obj file for detail.
    int pi(int i, int vn) { return i < 0 ? vn + i : i > 0 ? i - 1 : -1; }
    Ind pind(char *&t) {
        Ind i;
        ss(t);
        i.p = pi(atoi(t), int(geo.ps.size()));
        sc(t);
        if (t++[0] != '/') { return i; }
        i.t = pi(atoi(t), int(geo.ts.size()));
        sc(t);
        if (t++[0] != '/') { return i; }
        i.n = pi(atoi(t), int(geo.ns.size()));
        sc(t);
        return i;
    }

    // Parses a string
    void nn(char *&t, char name[]) { sscanf(t, "%s", name); };

    // Parses .mtl file
    void loadmtl(std::string p, std::vector<std::unique_ptr<Tex<F>>>& ts) {
        std::ifstream f(p);
        char l[4096], name[256];
        std::cout << "Loading MTL file: " << p << std::endl;
        while (f.getline(l, 4096)) {
            auto *t = l;
            ss(t);
            if (cm(t, "newmtl", 6)) {
                nn(t += 7, name);
                msmap[name] = int(ms.size());
                ms.emplace_back();
                continue;
            }
            if (ms.empty()) {
                continue;
            }
            auto& m = ms.back();
            if      (cm(t, "Kd", 2))     { m.Kd = nv(t += 3); }
            else if (cm(t, "Ks", 2))     { m.Ks = nv(t += 3); }
            else if (cm(t, "Ni", 2))     { m.Ni = nf(t += 3); }
            else if (cm(t, "Ns", 2))     { m.Ns = nf(t += 3); }
            else if (cm(t, "aniso", 5))  { m.an = nf(t += 5); }
            else if (cm(t, "Ke", 2))     {
                m.Ke = nv(t += 3);
            }
            else if (cm(t, "illum", 5))  { m.illum = ni(t += 6); }
            else if (cm(t, "map_Kd", 6)) {
                nn(t += 7, name);
                auto it = tsmap.find(name);
                if (it != tsmap.end()) {
                    m.mapKd = ts[it->second].get();
                    continue;
                }
                tsmap[name] = int(ts.size());
                ts.emplace_back(new Tex<F>);
                ts.back()->name = name;
                ts.back()->loadpng(sanitizeSeparator((std::filesystem::path(p).remove_filename() / name).string()));
                m.mapKd = ts.back().get();
            }
            else {
                continue;
            }
        }
    }
};

// Parse OBJ file
template <typename F>
void parseObj(std::string p, Geo<F>& geo, std::vector<std::unique_ptr<Tex<F>>>& ts,
    const std::function<void(const MatParams<F>& m, const std::vector<Ind>& fs)>& processMesh) {
    WavefrontOBJParser parser(geo, ts);
    parser.parse(p, processMesh);
}

// ----------------------------------------------------------------------------

template <typename F>
struct Tri {
    USE_RENDER_PRIMITIVE_TYPE(F);

    V p1;       // One vertex of the triangle
    V e1, e2;   // Two edges indident to p1
    V n;        // Normal
    B b;        // Bound of the triangle
    V c;        // Center of the bound
    int oi;     // Object index
    int fi;     // Face index

    Tri(const V& p1, const V& p2, const V& p3, int oi, int fi)
        : p1(p1), oi(oi), fi(fi) {
        e1 = p2 - p1;
        e2 = p3 - p1;
        n = norm(cross(p2 - p1, p3 - p1));
        b = merge(b, p1);
        b = merge(b, p2);
        b = merge(b, p3);
        c = b.center();
    }

    // Checks intersection with a ray [Möller & Trumbore 1997]
    struct Hit {
        F t;     // Distance to the triangle
        F u, v;  // Hitpoint in barycentric coodinates
    };
    std::optional<Hit> isect(const Ray& r, F tl, F th) const {
        V p = cross(r.d, e2), tv = r.o - p1;
        V q = cross(tv, e1);
        F d = dot(e1, p);
        F ad = abs(d);
        F s = copysign(F(1), d);
        F u = dot(tv, p) * s, v = dot(r.d, q) * s;
        if (ad < 1e-8 || u < 0 || v < 0 || u + v > ad) {
            return {};
        }
        F t = dot(e2, q) / d;
        if (t < tl || th < t) {
            return {};
        }
        return Hit{ t, u / ad, v / ad };
    }
};

template <typename F>
class Accel {
private:
    USE_RENDER_PRIMITIVE_TYPE(F);

private:
    // BVH node
    struct Node {
        B b;            // Bound of the node
        bool leaf = 0;  // True if the node is leaf
        int s, e;       // Range of triangle indices (valid only in leaf nodes)
        int c1, c2;     // Index to the child nodes
    };
    std::vector<Node> ns;    // Nodes
    std::vector<Tri<F>> trs; // Triangles
    std::vector<int> ti;     // Triangle indices

public:
    // Builds BVH
    void build(std::vector<Tri<F>>&& trs_) {
        trs = trs_;
        const int nt = int(trs.size());          // Number of triangles
        std::queue<std::tuple<int, int, int>> q; // Queue for traversal (node index, start, end)
        q.push({0, 0, nt});             // Initialize the queue with root node
        ns.assign(2 * nt - 1, {});      // Maximum number of nodes: 2*nt-1
        ti.assign(nt, 0);
        std::iota(ti.begin(), ti.end(), 0);
        std::mutex mu;                  // For concurrent queue
        std::condition_variable cv;     // For concurrent queue
        std::atomic<int> pr = 0;        // Processed triangles
        std::atomic<int> nn = 1;        // Number of current nodes
        bool done = 0;                  // True if the build process is done

        std::cout << "Building acceleration structure" << std::endl;
        auto process = [&]() {
            while (!done) {
                // Each step construct a node for the triangles ranges in [s,e)
                auto [ni, s, e] = [&]() -> std::tuple<int, int, int> {
                    std::unique_lock<std::mutex> lk(mu);
                    if (!done && q.empty()) {
                        cv.wait(lk, [&]() { return done || !q.empty(); });
                    }
                    if (done)
                        return {};
                    auto v = q.front();
                    q.pop();
                    return v;
                }();
                if (done) {
                    break;
                }
                // Calculate the bound for the node
                Node& n = ns[ni];
                for (int i = s; i < e; i++) {
                    n.b = merge(n.b, trs[ti[i]].b);
                }
                // Function to sort the triangles according to the given axis
                auto st = [&, s = s, e = e](int ax) {
                    auto cmp = [&](int i1, int i2) {
                        return trs[i1].c[ax] < trs[i2].c[ax];
                    };
                    std::sort(&ti[s], &ti[e - 1] + 1, cmp);
                };
                // Function to create a leaf node
                auto lf = [&, s = s, e = e]() {
                    n.leaf = 1;
                    n.s = s;
                    n.e = e;
                    pr += e - s;
                    if (pr == int(trs.size())) {
                        std::unique_lock<std::mutex> lk(mu);
                        done = 1;
                        cv.notify_all();
                    }
                };
                // Create a leaf node if the number of triangle is 1
                if (e - s < 2) {
                    lf();
                    continue;
                }
                // Selects a split axis and position according to SAH
                F b = Inf;
                int bi, ba;
                for (int a = 0; a < 3; a++) {
                    thread_local std::vector<F> l(nt + 1), r(nt + 1);
                    st(a);
                    B bl, br;
                    for (int i = 0; i <= e - s; i++) {
                        int j = e - s - i;
                        l[i] = bl.sa() * i;
                        r[j] = br.sa() * i;
                        bl = i < e - s ? merge(bl, trs[ti[s + i]].b) : bl;
                        br = j > 0 ? merge(br, trs[ti[s + j - 1]].b) : br;
                    }
                    for (int i = 1; i < e - s; i++) {
                        F c = 1 + (l[i] + r[i]) / n.b.sa();
                        if (c < b) {
                            b = c;
                            bi = i;
                            ba = a;
                        }
                    }
                }
                if (b > e - s) {
                    lf();
                    continue;
                }
                st(ba);
                int m = s + bi;
                std::unique_lock<std::mutex> lk(mu);
                q.push({n.c1 = nn++, s, m});
                q.push({n.c2 = nn++, m, e});
                cv.notify_one();
            }
        };
        std::vector<std::thread> ths(omp_get_max_threads());
        for (auto& th : ths) {
            th = std::thread(process);
        }
        for (auto& th : ths) {
            th.join();
        }
    }

    // Checks intersection to a ray
    struct Hit {
        F hitT;             // Minimum distance to the hit point
        const Tri<F>* tri;  // Intersected triangle
        F u, v;             // Barycentric coordinates
    };
    std::optional<Hit> isect(const Ray& r, F tl, F th) const {
        std::optional<Tri<F>::Hit> mh, h;
        int mi, s[99]{}, si = 0;
        while (si >= 0) {
            auto& n = ns.at(s[si--]);
            if (!n.b.isect(r, tl, th)) {
                continue;
            }
            if (!n.leaf) {
                s[++si] = n.c1;
                s[++si] = n.c2;
                continue;
            }
            for (int i = n.s; i < n.e; i++) {
                if (h = trs[ti[i]].isect(r, tl, th)) {
                    mh = h;
                    th = h->t;
                    mi = i;
                }
            }
        }
        if (!mh) {
            return {};
        }
        const auto& tr = trs[ti[mi]];
        return Hit{ th, &tr, mh->u, mh->v };
    }
};

// ----------------------------------------------------------------------------

template <typename F>
using EnvParams = std::optional<Params<EnvL<F>>>;

template <typename F>
using SensorParams = std::variant<Params<PinholeE<F>>, Params<RealisticE<F>>>;

template <typename F>
class Scene {
private:
    USE_RENDER_PRIMITIVE_TYPE(F);

private:
    Geo geo;                              // Scene geometry
    std::vector<Obj<F>> os;               // Scene objects
    std::vector<std::unique_ptr<Tex>> ts; // Textures
    std::vector<int> Ls;                  // Light indices
    Accel<F> accel;                       // Acceleration structure
    int envIndex = -1;                    // Index of environment light
    int sensorIndex = -1;                 // Index of sensor

public:
    Scene(std::string objFilePath, const EnvParams<F>& envParams, const SensorParams<F>& sensorParams) {
        // Setup sensor
        std::visit([&](auto&& args) {
            using T = typename std::decay_t<decltype(args)>::Base;
            sensorIndex = int(os.size());
            os.emplace_back(T(args));
        }, sensorParams);

        // Setup environment light (if available)
        if (envParams) {
            envIndex = int(os.size());
            Ls.push_back(envIndex);
            os.emplace_back(EnvL<F>(*envParams));
        }

        // Load scene from .obj file
        parseObj<F>(objFilePath, geo, ts, [&](const MatParams<F>& m, const std::vector<Ind>& fs) {
            // Create scene objects according to the loaded material types
            if (m.illum == 7) {
                // Fres
                os.emplace_back(fs, Fres<F>(m.Ni));
            }
            else if (m.illum == 5) {
                // PRefl
                os.emplace_back(fs, PRefl<F>());
            }
            else {
                // NonS
                if (m.Ke.m() > 0) {
                    Ls.push_back(int(os.size()));
                    os.emplace_back(fs, D<F>(m.Kd, m.mapKd), G<F>(m.Ks, m.Ns, m.an), AreaL<F>(m.Ke, geo, fs));
                }
                else {
                    os.emplace_back(fs, D<F>(m.Kd, m.mapKd), G<F>(m.Ks, m.Ns, m.an));
                }
            }
        });

        // Build acceleration structure
        std::vector<Tri<F>> trs;
        for (size_t oi = 0; oi < os.size(); oi++) {
            const auto& fs = os[oi].fs;
            for (size_t fi = 0; fi < fs.size(); fi += 3) {
                const V a = geo.ps[fs[fi].p];
                const V b = geo.ps[fs[fi + 1].p];
                const V c = geo.ps[fs[fi + 2].p];
                trs.emplace_back(a, b, c, int(oi), int(fi));
            }
        }
        accel.build(std::move(trs));
    }

    // Sensor object
    const Obj<F>* sensor() const {
        assert(sensorIndex >= 0);
        return &os.at(sensorIndex);
    }

    // Light sampling
    const Obj<F>* envL() const  {
        return envIndex >= 0 ? &os.at(envIndex) : nullptr;
    }
    std::tuple<const Obj<F>*, F> sampL(Rng& rn) const {
        const int n = int(Ls.size());
        const int i = std::clamp(int(rn.u() * n), 0, n - 1);
        return { &os.at(Ls[i]), F(1) / F(n) };
    }
    F pdfL() const {
        return F(1) / F(Ls.size());
    }

    // Intersection
    struct Hit {
        Surf sp;         // Surface information of the hit point
        const Obj<F>* o; // Object at the hit point
    };
    std::optional<Hit> isect(const Ray& r, F tl = Eps, F th = Inf, bool useenv = true) const {
        const auto hit = accel.isect(r, tl, th);
        if (!hit) {
            // Use environment light if available
            if (!useenv) {
                return {};
            }
            const auto* eL = envL();
            if (!eL) {
                return {};
            }
            return Hit{ Surf{}, eL };
        }
        const auto[hitT, tri, u, v] = *hit;
        const V p = r.o + hitT * r.d;
        const auto& o = os[tri->oi];
        const auto& i = o.fs[tri->fi];
        const auto& j = o.fs[tri->fi + 1];
        const auto& k = o.fs[tri->fi + 2];
        const V n = i.n < 0 ? tri->n : norm(intp(geo.ns[i.n], geo.ns[j.n], geo.ns[k.n], u, v));
        const V t = i.t < 0 ? 0 : intp(geo.ts[i.t], geo.ts[j.t], geo.ts[k.t], u, v);
        return Hit{ Surf{p, n, t}, &o };
    }
};

// ----------------------------------------------------------------------------

template <typename F>
void render(int argc, char** v) {
    USE_RENDER_PRIMITIVE_TYPE(F);

    const int ns = atoi(v[5]);        // Number of samples per pixel
    const int maxLength = atoi(v[6]); // Maximum path length
    const int w = atoi(v[8]);         // Output image width
    const int h = atoi(v[9]);         // Output image height

    // Path to .obj file
    const std::string objPath(v[1]);

    // Parameters for environment light
    const auto envParams = [&]() -> EnvParams<F> {
        const bool useEnvL = !std::string(v[2]).empty();
        if (!useEnvL) {
            return {};
        }
        return Params<EnvL<F>>{ v[2], F(atof(v[7])) };
    }();

    // Parameters for sensor
    const auto sensorParams = [&]() -> SensorParams<F> {
        const bool usePinholeE = std::string(v[3]).empty();
        const V eye   (F(atof(v[10])), F(atof(v[11])), F(atof(v[12])));
        const V center(F(atof(v[13])), F(atof(v[14])), F(atof(v[15])));
        const V up(0, 1, 0);
        const F aspect = F(w) / F(h);
        const F vfov = F(atof(v[16]));
        if (usePinholeE) {
            return Params<PinholeE<F>>{ eye, center, up, vfov, aspect };
        }
        return Params<RealisticE<F>>{ v[3], eye, center, up, vfov,
            F(atof(v[17])), F(atof(v[18])), F(atof(v[19])), aspect };
    }();

    // Scene setup
    Scene<F> scene(objPath, envParams, sensorParams);

    // Render an image with path tracing
    auto estimatePixelL = [&](Rng& rn, const V& uv) {
        const auto* obj = scene.sensor();
        V L;                // Estimated radiance
        V throughput(1);    // Throughput
        V wi = uv;          // Indident direction
        Surf sp;            // Surface information
        int length = 0;     // Path length

        while (length < maxLength && obj->sampComp(rn, sp, length==0, wi, [&](const auto& comp, F pcs) -> bool {
            using T = std::decay_t<decltype(comp)>;
            
            // Sample a ray
            const auto sampled = comp.samp(rn, sp, wi);
            if (!sampled) {
                return false;
            }
            const auto[r, w] = *sampled;

            // NEE
            if constexpr (is_one_of_v<T, NonSpecular<F>>) {
                auto[objL, pLs] = scene.sampL(rn);
                objL->visitComp<Light<F>>([&, pLs = pLs](const auto& compL) -> bool {
                    auto sL = compL.sampL(rn, sp);
                    if (!sL) {
                        return false;
                    }
                    auto[wo, d, fL, pL] = *sL;
                    if (scene.isect({ sp.p, wo }, Eps, d * (1 - Eps), false)) {
                        return false;
                    }
                    const auto f = comp.ev(sp, wi, wo) * fL;
                    const auto p = comp.pdf(sp, wi, wo) + pL * pLs;
                    L = L + throughput * f / p / pcs;
                    return true;
                });
            }

            // Update throughput
            throughput = throughput * w / pcs;

            // Intersection with the next surface
            auto hi = scene.isect(r);
            if (!hi) {
                return false;
            }

            // Accumulate contribution from the light
            auto[spH, objH] = *hi;
            objH->visitComp<Light<F>>([&,spH=spH,r=r](const auto& compL) -> bool {
                const auto f = compL.ev(spH, {}, -r.d);
                if constexpr (!is_one_of_v<T, NonSpecular<F>>) {
                    L = L + throughput * f;
                }
                else {
                    const auto p = compL.pdfL(sp, spH, -r.d) * scene.pdfL() / comp.pdf(sp, wi, r.d) + F(1);
                    L = L + throughput * f / p;
                }
                return true;
            });

            // Russian roulette
            if (length > 3) {
                F q = std::max(F(.2), F(1) - throughput.m());
                throughput = throughput / (F(1) - q);
                if (rn.u() < q) {
                    return false;
                }
            }

            // Update information
            length++;
            wi = -r.d;
            obj = objH;
            sp = spH;

            return true;
        }));

        return L;
    };
    std::vector<V> film(w * h, V());
    const long long samples = (long long)(ns) * w * h;
    constexpr long long interval = 10000;
    std::atomic<long long> processed = 0;
    #pragma omp parallel for
    for (int i = 0; i < w * h; i++) {
        thread_local Rng rn(42 + omp_get_thread_num());
        for (int j = 0; j < ns; j++) {
            // Sample random position in a pixel
            const V rp((i%w + rn.u()) / w, (i/w + rn.u()) / h, 0);
            film[i] = film[i] + estimatePixelL(rn, rp) / F(ns);

            // Progress report
            thread_local long long count = 0;
            if (++count >= interval) {
                processed += interval;
                count = 0;
                if (omp_get_thread_num() == 0) {
                    const double progress = (double)(processed) / samples * 100.0;
                    printf("\rRendering: %.1f %%", progress);
                    std::cout << std::flush;
                }
            }
        }
    }
    printf("\rRendering: 100.0%%\n");

    // Save rendered image
    size_t spos = objPath.find_last_of("/");
    if (spos != std::string::npos) {
        spos = spos + 1;
    } else {
        spos = 0;
    }
    size_t epos = objPath.find_last_of(".");
    std::string output_path = objPath.substr(spos, epos - spos) + ".pfm";

    //std::cout << "Saving image: " << v[4] << std::endl;
    std::cout << "Saving image: " << output_path << std::endl;
    FILE *f = fopen(output_path.c_str(), "wb");
    fprintf(f, "PF\n%d %d\n-1\n", w, h);
    std::vector<float> d(w * h * 3);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            for (int i = 0; i < 3; i++) {
                d[3*(y*w+x)+i] = float(film[(h-1-y)*w+(w-1-x)][i]);
            }
        }
    }
    fwrite(d.data(), 4, d.size(), f);
    fclose(f);
}

int main(int argc, char** argv) {
#if _DEBUG && 0
    omp_set_num_threads(1);
#endif
    render<double>(argc, argv);
    //render<float>(argc, argv);
    return EXIT_SUCCESS;
}
