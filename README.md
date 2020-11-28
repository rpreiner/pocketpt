# pocketpt
A single-source GLSL path tracer in 111 lines of code, based on [Kevin Beason's smallpt](http://kevinbeason.com/smallpt).

Platform: Windows

Make: `g++ -O3 pocketpt.cpp -o pocketpt -lopengl32 -lgdi32`

Run:  `./pocketpt <samplesPerPixel=1000> <y-resolution=400>`
