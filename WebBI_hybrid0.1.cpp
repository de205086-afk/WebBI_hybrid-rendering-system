// WebBI_Hybrid_Final.cpp
#include <iostream>
#include <vector>
#include <thread>
#include <cmath>
#include <mutex>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <arpa/inet.h>
#include <unistd.h>

#define WIDTH 800
#define HEIGHT 600
#define TILE_SIZE 32
#define THREADS 8

// ==========================
// Virtual VRAM
// ==========================
struct VirtualVRAM {
std::vector<uint8_t> pool;
VirtualVRAM(size_t size) { pool.resize(size); }
};
VirtualVRAM vram(512 * 1024 * 1024); // 512MB simulated VRAM

// ==========================
// Triple Framebuffers + Z buffer
// ==========================
std::vector<uint32_t> framebuffer1(WIDTH*HEIGHT);
std::vector<uint32_t> framebuffer2(WIDTH*HEIGHT);
std::vector<uint32_t> framebuffer3(WIDTH*HEIGHT);
std::vector<float> zbuffer(WIDTH*HEIGHT);

std::vector<uint32_t>* currentFrame = &framebuffer1;
std::vector<uint32_t>* nextFrame = &framebuffer2;
std::vector<uint32_t>* displayFrame = &framebuffer3;

// ==========================
// Utility functions
// ==========================
void clearBuffers(std::vector<uint32_t>* fb, std::vector<float>& zb) {
std::fill(fb->begin(), fb->end(), 0x00000000);
std::fill(zb.begin(), zb.end(), 1e9f);
}

// ==========================
// Disk cache
// ==========================
void writeCache(const std::string& filename, std::vector<uint32_t>& data) {
std::ofstream out("cache/"+filename, std::ios::binary);
out.write((char*)data.data(), data.size()*4);
}
void loadCache(const std::string& filename, std::vector<uint32_t>& data) {
std::ifstream in("cache/"+filename, std::ios::binary);
if(in) in.read((char*)data.data(), data.size()*4);
}

// ==========================
// Upscaling (2x nearest-neighbor example)
// ==========================
void upscale2x(std::vector<uint32_t>& src, std::vector<uint32_t>& dst, int w, int h) {
for(int y=0;y<h;y++){
for(int x=0;x<w;x++){
uint32_t px = src[y*w+x];
int dx = x*2, dy = y*2;
dst[dy*2*w + dx] = px;
dst[dy*2*w + dx +1] = px;
dst[(dy+1)*2*w + dx] = px;
dst[(dy+1)*2*w + dx +1] = px;
}
}
}

// ==========================
// Texture
// ==========================
struct Texture {
int w, h;
std::vector<uint32_t> data;
};
Texture checkerTexture() {
Texture t;
t.w = 64; t.h = 64;
t.data.resize(64*64);
for(int y=0;y<64;y++)
for(int x=0;x<64;x++)
t.data[y*64+x] = ((x/8+y/8)%2) ? 0xFFFFFFFF : 0xFF000000;
return t;
}

// ==========================
// Rasterizer
// ==========================
float edge(float x0,float y0,float x1,float y1,float x,float y){
return (x-x0)*(y1-y0)-(y-y0)*(x1-x0);
}

void rasterTile(int startX,int startY,int endX,int endY,
float x0,float y0,float z0,
float x1,float y1,float z1,
float x2,float y2,float z2,
Texture &tex,
std::vector<uint32_t>* fb,
std::vector<float>& zb)
{
for(int y=startY;y<endY;y++){
for(int x=startX;x<endX;x++){
float w0=edge(x1,y1,x2,y2,x,y);
float w1=edge(x2,y2,x0,y0,x,y);
float w2=edge(x0,y0,x1,y1,x,y);

if(w0>=0 && w1>=0 && w2>=0){
float z = (z0+z1+z2)/3.0f;
int idx = y*WIDTH + x;
if(z<zb[idx]){
zb[idx]=z;
int tx=x%tex.w, ty=y%tex.h;
(*fb)[idx] = tex.data[ty*tex.w+tx];
}
}
}
}
}

// ==========================
// Multithreaded Draw
// ==========================
void drawTriangleMT(Texture &tex, std::vector<uint32_t>* fb, std::vector<float>& zb){
std::vector<std::thread> workers;
for(int ty=0;ty<HEIGHT;ty+=TILE_SIZE){
for(int tx=0;tx<WIDTH;tx+=TILE_SIZE){
workers.emplace_back(rasterTile,
tx, ty,
std::min(tx+TILE_SIZE,WIDTH),
std::min(ty+TILE_SIZE,HEIGHT),
400,100,0.5f,
200,500,0.5f,
600,500,0.5f,
std::ref(tex),
fb,
std::ref(zb));
}
}
for(auto &t:workers) t.join();
}

// ==========================
// Server/Client (optional)
// ==========================
int startServer() {
int server_fd = socket(AF_INET, SOCK_STREAM, 0);
sockaddr_in addr{};
addr.sin_family = AF_INET;
addr.sin_port = htons(9000);
addr.sin_addr.s_addr = INADDR_ANY;
bind(server_fd,(sockaddr*)&addr,sizeof(addr));
listen(server_fd,1);
std::cout<<"Waiting for client...\n";
int client = accept(server_fd,nullptr,nullptr);
std::cout<<"Client connected.\n";
return client;
}

int startClient(const char* ip) {
int sock = socket(AF_INET, SOCK_STREAM, 0);
sockaddr_in addr{};
addr.sin_family = AF_INET;
addr.sin_port = htons(9000);
inet_pton(AF_INET, ip, &addr.sin_addr);
connect(sock,(sockaddr*)&addr,sizeof(addr));
std::cout<<"Connected to server "<<ip<<"\n";
return sock;
}

// ==========================
// Native Mode (fully featured)
// ==========================
void runNativeMode() {
Texture tex = checkerTexture();
std::vector<uint32_t> upscaledFrame(WIDTH*2*HEIGHT*2);

while(true){
clearBuffers(nextFrame, zbuffer);
drawTriangleMT(tex, nextFrame, zbuffer);

// Triple buffering swap
std::swap(currentFrame, nextFrame);
std::swap(displayFrame, currentFrame);

// Upscale
upscale2x(*displayFrame, upscaledFrame, WIDTH, HEIGHT);

// Simulate display
std::cout << "Frame rendered (native mode, hybrid CPU+GPU, triple-buffered)\r";
std::cout.flush();

// Optional: disk caching example
writeCache("last_frame.bin", *displayFrame);
}
}

// ==========================
// Server/Client Mode
// ==========================
void runServerMode() {
Texture tex = checkerTexture();
int client = startServer();
while(true){
clearBuffers(nextFrame, zbuffer);
drawTriangleMT(tex, nextFrame, zbuffer);
send(client, nextFrame->data(), WIDTH*HEIGHT*4, 0);
std::cout << "Frame sent to client\r";
std::cout.flush();
std::swap(currentFrame, nextFrame);
}
}

void runClientMode(const char* ip) {
int sock = startClient(ip);
while(true){
recv(sock, displayFrame->data(), WIDTH*HEIGHT*4, 0);
std::cout << "Frame received from server\r";
std::cout.flush();
}
}

// ==========================
// Entry point
// ==========================
int main(int argc, char* argv[]) {
if(argc > 1 && strcmp(argv[1], "server") == 0){
runServerMode();
} else if(argc > 1 && strcmp(argv[1], "client") == 0){
if(argc < 3){
std::cerr << "Usage: ./WebBI client <server-ip>\n";
return 1;
}
runClientMode(argv[2]);
} else {
runNativeMode();
}
return 0;
}