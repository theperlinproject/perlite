// Renders the Bedrock generator top-down: bedrock_terrain_dump <seed> <radius> [chunkX chunkZ] writes map.ppm
// and prints the spawn, the time per chunk and the most common top blocks. Needs the vanilla behavior pack.
#include "world/level/levelgen/bedrock/Overworld.hpp"
#include "world/level/block/BlockCatalog.hpp"
#include "world/level/block/Blocks.hpp"
#include <chrono>
#include <cstdio>
#include <algorithm>
#include <map>
using namespace perlite;
int main(int argc,char**argv){
  BlockCatalog::Instance().LoadBuiltin();
  ServerConfig c; c.seed = argc>1? std::strtoll(argv[1],nullptr,10):0; int R = argc>2?atoi(argv[2]):6;
  int ox = argc>3?atoi(argv[3]):0, oz = argc>4?atoi(argv[4]):0;
  std::string err; auto b = bedrock::OverworldBackend::Create(c, err); if(!b){printf("%s\n",err.c_str());return 1;}
  int x,y,z; b->FindSpawn(x,y,z); printf("spawn %d %d %d\n",x,y,z);
  if(getenv("PROBE")){ for(int i=0;i<8;i++){int px=i*137-500,pz=i*91-300; perlite::bedrock::BiomeVolume v; b->Build(px>>4,pz>>4,&v); for(int py: {-30,40,64,100}) printf("probe %d %d %d direct=%d volume=%d\n",px,py,pz,b->BiomeAt(px,py,pz),v.IdAt(px&15,py,pz&15));} return 0;}
  int W=R*2*16; std::vector<unsigned char> img(W*W*3);
  std::map<std::string,int> counts, biomes; auto t0=std::chrono::steady_clock::now();
  for(int cx=-R;cx<R;cx++)for(int cz=-R;cz<R;cz++){
    perlite::bedrock::BiomeVolume vol; auto ch=b->Build(cx+ox,cz+oz,&vol);
    for(int lx=0;lx<16;lx++)for(int lz=0;lz<16;lz++){
      int yy=ch.minY+ch.height-1; uint16_t blk=0; for(;yy>=ch.minY;yy--){blk=ch.Get(lx,yy,lz); if(blk) break;}
      std::string n=BlockRegistry::Instance().Name(blk); counts[n]++; { int bi=perlite::bedrock::BiomeById(vol.IdAt(lx,yy,lz)); biomes[bi>=0?perlite::bedrock::kBiomeTable[bi].pack:"?"]++; }
      unsigned r=128,g=128,bl=128;
      if(n.find("water")!=std::string::npos){r=40;g=70;bl=200;} else if(n.find("grass")!=std::string::npos){r=80;g=160;bl=60;}
      else if(n.find("sand")!=std::string::npos){r=220;g=210;bl=150;} else if(n.find("snow")!=std::string::npos){r=240;g=240;bl=250;}
      else if(n.find("ice")!=std::string::npos){r=160;g=200;bl=250;} else if(n.find("terracotta")!=std::string::npos||n.find("clay")!=std::string::npos){r=190;g=100;bl=60;}
      else if(n.find("gravel")!=std::string::npos){r=150;g=140;bl=140;} else if(n.find("dirt")!=std::string::npos||n.find("podzol")!=std::string::npos||n.find("mud")!=std::string::npos){r=120;g=85;bl=50;}
      else if(n.find("lava")!=std::string::npos){r=250;g=100;bl=0;}
      float shade=0.6f+0.4f*std::min(1.0f,std::max(0.0f,(yy-40)/120.0f));
      int px=(cx+R)*16+lx, pz=(cz+R)*16+lz; unsigned char*p=&img[(pz*W+px)*3]; p[0]=r*shade;p[1]=g*shade;p[2]=bl*shade;}
  }
  double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();
  printf("%d chunks %.1f ms each\n",4*R*R,ms/(4*R*R));
  FILE*f=fopen("map.ppm","wb"); fprintf(f,"P6 %d %d 255\n",W,W); fwrite(img.data(),1,img.size(),f); fclose(f);
  std::vector<std::pair<int,std::string>> v; for(auto&[k,n]:counts) v.push_back({n,k}); std::sort(v.rbegin(),v.rend()); for(int i=0;i<12&&i<(int)v.size();i++) printf("%s %d\n",v[i].second.c_str(),v[i].first);
  v.clear(); for(auto&[k,n]:biomes) v.push_back({n,k}); std::sort(v.rbegin(),v.rend()); for(int i=0;i<16&&i<(int)v.size();i++) printf("  biome %s %d\n",v[i].second.c_str(),v[i].first);
}
