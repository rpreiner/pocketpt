#include <stdlib.h>     // pocketpt, single-source GLSL path tracer by Reinhold Preiner, 2020
#include <stdio.h>      // based on smallpt by Kevin Beason
#include <windows.h>    // Make: g++ -O3 pocketpt.cpp -o pocketpt -lopengl32 -lgdi32
#include <GL/GL.h>      // Usage: ./pocketpt <samplesPerPixel> <y-resolution>, e.g.: ./pocketpt 10000 600
#define GLSL(...) "#version 460\n" #__VA_ARGS__
template<class R = void, class ... A> R glFunc (const char* n, A... a) { return ((R (__stdcall* )(A...))
    ((void* (__stdcall*)(const char*)) GetProcAddress(LoadLibraryW(L"opengl32.dll"), "wglGetProcAddress"))(n)) (a...); };
float spheres[] = {  // center.xyz, radius  |  emmission.xyz, 0  |  color.rgb, refltype     
    -1e5 - 2.6, 0, 0, 1e5,  0, 0, 0, 0,  .75, .25, .25,  1, // Left (DIFFUSE)
    1e5 + 2.6, 0, 0, 1e5,   0, 0, 0, 0,  .25, .25, .75,  1, // Right
    0, 1e5 + 2, 0, 1e5,     0, 0, 0, 0,  .75, .75, .75,  1, // Top
    0,-1e5 - 2, 0, 1e5,     0, 0, 0, 0,  .75, .75, .75,  1, // Bottom
    0, 0, -1e5 - 2.8, 1e5,  0, 0, 0, 0,  .75, .75, .75,  1, // Back 
    0, 0, 1e5 + 7.9, 1e5,   0, 0, 0, 0,  0, 0, 0,        1, // Front
    -1.3, -1.2, -1.3, 0.8,  0, 0, 0, 0,  .999,.999,.999, 2, // REFLECTIVE
    1.3, -1.2, -0.2, 0.8,   0, 0, 0, 0,  .999,.999,.999, 3, // REFRACTIVE
    0, 11.96, 0, 10,        12,12,12,0,  0, 0, 0,        1, // Light
};
int main(int argc, char *argv[]) {
    int spp = argc>1 ? atoi(argv[1]) : 1000, resy = argc>2 ? atoi(argv[2]) : 400, resx = resy*3/2;	// image resolution
    HDC hdc = GetDC(0);
    static PIXELFORMATDESCRIPTOR pfd;
    SetPixelFormat(hdc, ChoosePixelFormat(hdc, &pfd), &pfd);
    wglMakeCurrent(hdc, wglCreateContext(hdc));
    struct { int Sph, Rad; } buf;                                                       // GPU pixel buffer
    glFunc("glGenBuffers", 2, &buf);
    glFunc("glNamedBufferData", buf.Sph, sizeof(spheres), spheres, 0x88E4);             // GL_STATIC_DRAW
    glFunc("glNamedBufferData", buf.Rad, 4*resx*resy*sizeof(float), 0, 0x88E4); 
    glFunc("glBindBufferBase", 0x90D2, 0, buf.Sph);                                     // GL_SHADER_STORAGE_BUFFER
    glFunc("glBindBufferBase", 0x90D2, 1, buf.Rad);  
    int p=glFunc<int>("glCreateProgram"), s=glFunc<int>("glCreateShader", 0x91B9);      // GL_COMPUTE_SHADER
    const char* src = GLSL(
        layout(local_size_x = 16, local_size_y = 16) in;
        struct Ray { vec3 o; vec3 d; };
        struct Sphere { vec4 geo; vec3 e; vec4 c; } obj;
        layout(std430, binding = 0) buffer b0 { Sphere spheres[]; };
        layout(std430, binding = 1) buffer b1 { vec4 accRad[]; };
        uniform uvec2 imgdim, samps;          // image dimensions and sample count
        vec3 rand01(uvec3 x){
            for (int i=3; i-->0;) x = ((x>>8U)^x.yzx)*1103515245U;
            return vec3(x)*(1.0/float(0xffffffffU));
        }
        void main() {
            uvec2 pix = gl_GlobalInvocationID.xy;
            if (pix.x >= imgdim.x || pix.y >= imgdim.y) return;
            uint gid = (imgdim.y - pix.y - 1) * imgdim.x + pix.x;
            if (samps.x == 0) accRad[gid] = vec4(0);                                        // initialize radiance buffer
            Ray cam = Ray(vec3(0, 0.52, 7.4), normalize(vec3(0, -0.06, -1)));               // define camera
            vec3 cx = normalize(cross(cam.d, abs(cam.d.y)<0.9 ? vec3(0,1,0) : vec3(0,0,1))), cy = cross(cx, cam.d);
            vec2 sdim = vec2(0.036, 0.024), rnd2 = 2*rand01(uvec3(pix, samps.x)).xy;        // vvv tent filter sample  
            vec2 tent = vec2(rnd2.x<1 ? sqrt(rnd2.x)-1 : 1-sqrt(2-rnd2.x), rnd2.y<1 ? sqrt(rnd2.y)-1 : 1-sqrt(2-rnd2.y));
            vec2 s = ((pix + 0.5 * (0.5 + vec2((samps.x/2)%2, samps.x%2) + tent)) / vec2(imgdim) - 0.5)*sdim;
            vec3 spos = cam.o + cx*s.x + cy*s.y, lc = cam.o + cam.d * 0.035, accrad=vec3(0), accmat=vec3(1);
            Ray r = Ray(lc, normalize(lc - spos));          // construct ray
            for (int depth = 0; depth < 64; depth++) {      // loop over ray bounces (max depth = 64)
                float d, inf = 1e20, t = inf, eps = 1e-4;   // intersect ray with scene
                for (int i = spheres.length(); i-->0;) { 
                    Sphere s = spheres[i];                  // perform intersection test in double precision
                    dvec3 oc = dvec3(s.geo.xyz) - r.o;      // Solve t^2*d.d + 2*t*(o-s).d + (o-s).(o-s)-r^2 = 0 
                    double b=dot(oc,r.d), det=b*b-dot(oc,oc)+s.geo.w*s.geo.w; 
                    if (det < 0) continue; else det=sqrt(det); 
                    d = (d = float(b-det))>eps ? d : ((d=float(b+det))>eps ? d : inf);
                    if(d < t) { t=d; obj = s; } 
                } 
                if (t < inf) {                  // object hit
                    vec3 x = r.o + r.d * float(t), n = normalize(x-obj.geo.xyz), nl = dot(n,r.d)<0 ? n : -n;
                    float p = max(max(obj.c.x, obj.c.y), obj.c.z);  // max reflectance
                    accrad += accmat*obj.e;
                    accmat *= obj.c.xyz;
                    vec3 rdir = reflect(r.d,n), rnd = rand01(uvec3(pix, samps.x*64 + depth));
                    if (depth > 5) if (rnd.z >= p) break; else accmat /= p;  // Russian Roulette
                    if (obj.c.w == 1) {         // DIFFUSE
                        float r1 = 2 * 3.141592653589793 * rnd.x, r2 = rnd.y, r2s = sqrt(r2);     // cosine sampling
                        vec3 w = nl, u = normalize((cross(abs(w.x)>0.1 ? vec3(0,1,0) : vec3(1,0,0), w))), v = cross(w,u);
                        r = Ray(x, normalize(u*cos(r1)*r2s + v * sin(r1)*r2s + w * sqrt(1 - r2)));
                    } else if (obj.c.w == 2) {  // SPECULAR
                        r = Ray(x, rdir);   
                    } else if (obj.c.w == 3) {  // REFRACTIVE
                        bool into = n==nl;
                        float cos2t, nc=1, nt=1.5, nnt = into ? nc/nt : nt/nc, ddn = dot(r.d,nl);
                        if ((cos2t = 1 - nnt * nnt*(1 - ddn * ddn)) >= 0) {  // Fresnel reflection/refraction
                            vec3 tdir = normalize(r.d*nnt - n * ((into ? 1 : -1)*(ddn*nnt + sqrt(cos2t))));
                            float a = nt - nc, b = nt + nc, R0 = a*a/(b*b), c = 1 - (into ? -ddn : dot(tdir,n));
                            float Re = R0 + (1 - R0)*c*c*c*c*c, Tr = 1 - Re, P = 0.25 + 0.5*Re, RP = Re/P, TP = Tr/(1-P);
                            r = Ray(x, rnd.x < P ? rdir : tdir);    // pick reflection with probability P
                            accmat *=  rnd.x < P ? RP : TP;         // energy compensation
                        } else r = Ray(x, rdir);                    // Total internal reflection
                    }
                }
            }
            accRad[gid] += vec4(accrad / samps.y, 0);   // <<< accumulate radiance   vvv write 8bit rgb gamma encoded color
            if (samps.x == samps.y-1) accRad[gid].xyz = pow(vec3(clamp(accRad[gid].xyz, 0, 1)), vec3(0.45)) * 255 + 0.5;
        });
    glFunc("glShaderSource", s, 1, &src, 0);
    glFunc("glCompileShader", s);
    glFunc("glAttachShader", p, s);
    glFunc("glLinkProgram", p);
    glFunc("glUseProgram", p);       
    glFunc("glUniform2ui", glFunc<int>("glGetUniformLocation", p, "imgdim"), resx, resy);
    for (int pass = 0; pass < spp; pass++) {        // sample rays
        glFunc("glUniform2ui", glFunc<int>("glGetUniformLocation", p, "samps"), pass, spp);
        glFunc("glDispatchCompute", (resx + 15) / 16, (resy + 15) / 16, 1);
        fprintf(stderr, "\rRendering (%d spp) %5.2f%%", spp, 100.0 * (pass+1) / spp);
        glFinish();
    }
    float* pixels = new float[4*resx*resy];         // read back buffer and write inverse sensor image to file
    glFunc("glGetNamedBufferSubData", buf.Rad, 0, 4*resx*resy*sizeof(float), pixels);
    FILE *file = fopen("image.ppm", "w");
    fprintf(file, "P3\n# spp: %d\n%d %d %d\n", spp, resx, resy, 255);
    for (int i = resx*resy; i-->0;) fprintf(file, "%d %d %d ", int(pixels[4*i]), int(pixels[4*i+1]), int(pixels[4*i+2]));
}