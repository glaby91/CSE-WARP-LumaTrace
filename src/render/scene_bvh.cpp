#include "scene_bvh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>

namespace lumatrace::render {
namespace {
using scene::Matrix4;
using scene::Vec3;

constexpr std::uint32_t kSahBinCount = 16;

Vec3 point(const Matrix4& m, Vec3 p) {
    const float w=m[3]*p.x+m[7]*p.y+m[11]*p.z+m[15];
    const float q=(std::abs(w)>1.e-12f&&std::abs(w-1.f)>1.e-6f)?1.f/w:1.f;
    return {(m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12])*q,
            (m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13])*q,
            (m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14])*q};
}

Vec3 normal(const Matrix4& m, Vec3 v) {
    const float a=m[0],b=m[4],c=m[8],d=m[1],e=m[5],f=m[9],g=m[2],h=m[6],i=m[10];
    const float A=e*i-f*h,B=f*g-d*i,C=d*h-e*g;
    const float D=c*h-b*i,E=a*i-c*g,F=b*g-a*h;
    const float G=b*f-c*e,H=c*d-a*f,I=a*e-b*d;
    Vec3 n{A*v.x+B*v.y+C*v.z,D*v.x+E*v.y+F*v.z,G*v.x+H*v.y+I*v.z};
    const float l=std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);
    if(l>1.e-12f){n.x/=l;n.y/=l;n.z/=l;}
    return n;
}

Vec3 centroid(const GpuTriangle& t) {
    return {(t.p0.x+t.p1.x+t.p2.x)/3.f,
            (t.p0.y+t.p1.y+t.p2.y)/3.f,
            (t.p0.z+t.p1.z+t.p2.z)/3.f};
}

struct BuildBounds {
    Vec3 lo{1.e30f,1.e30f,1.e30f};
    Vec3 hi{-1.e30f,-1.e30f,-1.e30f};
    bool valid=false;
    void add(Vec3 p) {
        lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};
        hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};
        valid=true;
    }
    void add(const BuildBounds& b) { if(b.valid){add(b.lo);add(b.hi);} }
};

float surfaceArea(const BuildBounds& b) {
    const float x=std::max(0.f,b.hi.x-b.lo.x);
    const float y=std::max(0.f,b.hi.y-b.lo.y);
    const float z=std::max(0.f,b.hi.z-b.lo.z);
    return 2.f*(x*y+y*z+z*x);
}

struct BinaryNode {
    BuildBounds bounds;
    std::uint32_t left=0,right=0,firstTriangle=0,triangleCount=0;
};
struct SahBin { BuildBounds bounds; std::uint32_t count=0; };

} // namespace

SceneBvh buildSceneBvh(const scene::Scene& source, std::uint32_t leafSize) {
    SceneBvh out;
    out.triangles.reserve(size_t(source.triangleCount));
    for(const auto& primitive:source.primitives) {
        const auto& idx=primitive.indices;
        for(size_t k=0;k+2<idx.size();k+=3) {
            const auto& a=primitive.vertices[idx[k]];
            const auto& b=primitive.vertices[idx[k+1]];
            const auto& c=primitive.vertices[idx[k+2]];
            GpuTriangle t{};
            t.p0=point(primitive.worldTransform,a.position);
            t.p1=point(primitive.worldTransform,b.position);
            t.p2=point(primitive.worldTransform,c.position);
            t.n0=normal(primitive.worldTransform,a.normal);
            t.n1=normal(primitive.worldTransform,b.normal);
            t.n2=normal(primitive.worldTransform,c.normal);
            t.uv0=a.uv0; t.uv1=b.uv0; t.uv2=c.uv0;
            t.material=std::uint32_t(std::max(0,primitive.material));
            t.triangleId=std::uint32_t(out.triangles.size());
            out.triangles.push_back(t);
        }
    }
    if(out.triangles.empty()) return out;

    leafSize=std::max(1u,leafSize);
    std::vector<std::uint32_t> order(out.triangles.size());
    std::iota(order.begin(),order.end(),0u);
    std::vector<BinaryNode> binary;
    binary.reserve(out.triangles.size()/leafSize*2u);

    const auto buildBinary=[&](auto&& self,std::uint32_t first,std::uint32_t count)->std::uint32_t {
        BuildBounds bounds,centroidBounds;
        for(std::uint32_t i=first;i<first+count;++i) {
            const auto& t=out.triangles[order[i]];
            bounds.add(t.p0);bounds.add(t.p1);bounds.add(t.p2);
            centroidBounds.add(centroid(t));
        }
        const std::uint32_t at=std::uint32_t(binary.size());
        binary.push_back({});
        binary[at].bounds=bounds;
        if(count<=leafSize) {
            binary[at].firstTriangle=first;
            binary[at].triangleCount=count;
            return at;
        }

        const float ext[3]={centroidBounds.hi.x-centroidBounds.lo.x,
                            centroidBounds.hi.y-centroidBounds.lo.y,
                            centroidBounds.hi.z-centroidBounds.lo.z};
        const float cmin[3]={centroidBounds.lo.x,centroidBounds.lo.y,centroidBounds.lo.z};
        std::uint32_t bestAxis=3,bestSplit=0;
        float bestCost=std::numeric_limits<float>::infinity();
        for(std::uint32_t axis=0;axis<3;++axis) {
            if(!(ext[axis]>1.e-12f)) continue;
            std::array<SahBin,kSahBinCount> bins{};
            for(std::uint32_t i=first;i<first+count;++i) {
                const std::uint32_t id=order[i];
                const Vec3 c=centroid(out.triangles[id]);
                const float coord=axis==0?c.x:axis==1?c.y:c.z;
                const float scaled=(coord-cmin[axis])*(float(kSahBinCount)/ext[axis]);
                const auto bin=std::min(kSahBinCount-1,std::uint32_t(std::max(0.f,scaled)));
                auto& dst=bins[bin];
                const auto& t=out.triangles[id];
                dst.bounds.add(t.p0);dst.bounds.add(t.p1);dst.bounds.add(t.p2);
                ++dst.count;
            }
            std::array<BuildBounds,kSahBinCount> leftBounds{},rightBounds{};
            std::array<std::uint32_t,kSahBinCount> leftCount{},rightCount{};
            BuildBounds runningBounds; std::uint32_t runningCount=0;
            for(std::uint32_t i=0;i<kSahBinCount;++i) {
                runningBounds.add(bins[i].bounds);runningCount+=bins[i].count;
                leftBounds[i]=runningBounds;leftCount[i]=runningCount;
            }
            runningBounds=BuildBounds{};runningCount=0;
            for(std::uint32_t i=kSahBinCount;i-->0;) {
                runningBounds.add(bins[i].bounds);runningCount+=bins[i].count;
                rightBounds[i]=runningBounds;rightCount[i]=runningCount;
            }
            for(std::uint32_t split=0;split+1<kSahBinCount;++split) {
                const auto nl=leftCount[split],nr=rightCount[split+1];
                if(nl==0||nr==0) continue;
                const float cost=surfaceArea(leftBounds[split])*float(nl)
                                +surfaceArea(rightBounds[split+1])*float(nr);
                if(cost<bestCost){bestCost=cost;bestAxis=axis;bestSplit=split;}
            }
        }

        std::uint32_t mid=first;
        if(bestAxis<3) {
            const float scale=float(kSahBinCount)/ext[bestAxis];
            auto pivot=std::partition(order.begin()+first,order.begin()+first+count,
                [&](std::uint32_t id) {
                    const Vec3 c=centroid(out.triangles[id]);
                    const float coord=bestAxis==0?c.x:bestAxis==1?c.y:c.z;
                    const float scaled=(coord-cmin[bestAxis])*scale;
                    const auto bin=std::min(kSahBinCount-1,std::uint32_t(std::max(0.f,scaled)));
                    return bin<=bestSplit;
                });
            mid=std::uint32_t(pivot-order.begin());
        }
        if(mid<=first||mid>=first+count) {
            const std::uint32_t axis=std::uint32_t(std::max_element(ext,ext+3)-ext);
            const std::uint32_t half=count/2;
            auto begin=order.begin()+first;
            std::nth_element(begin,begin+half,begin+count,[&](std::uint32_t l,std::uint32_t r) {
                const Vec3 a=centroid(out.triangles[l]),b=centroid(out.triangles[r]);
                const float ca=axis==0?a.x:axis==1?a.y:a.z;
                const float cb=axis==0?b.x:axis==1?b.y:b.z;
                return ca<cb;
            });
            mid=first+half;
        }
        const auto left=self(self,first,mid-first);
        const auto right=self(self,mid,first+count-mid);
        binary[at].left=left;binary[at].right=right;
        return at;
    };
    buildBinary(buildBinary,0,std::uint32_t(out.triangles.size()));

    out.triangleOrder=std::move(order);
    out.bounds.minimum=binary.front().bounds.lo;
    out.bounds.maximum=binary.front().bounds.hi;
    out.bounds.valid=true;

    std::function<std::uint32_t(std::uint32_t)> emitWide;
    emitWide=[&](std::uint32_t binaryIndex)->std::uint32_t {
        const auto& sourceNode=binary[binaryIndex];
        std::vector<std::uint32_t> children;
        if(sourceNode.triangleCount>0) children.push_back(binaryIndex);
        else children={sourceNode.left,sourceNode.right};
        while(children.size()<4) {
            std::size_t expand=children.size();
            float largestArea=-1.f;
            for(std::size_t i=0;i<children.size();++i) {
                const auto& child=binary[children[i]];
                if(child.triangleCount==0) {
                    const float a=surfaceArea(child.bounds);
                    if(a>largestArea){largestArea=a;expand=i;}
                }
            }
            if(expand==children.size()) break;
            const auto expanded=children[expand];
            children.erase(children.begin()+std::ptrdiff_t(expand));
            children.insert(children.begin()+std::ptrdiff_t(expand),binary[expanded].right);
            children.insert(children.begin()+std::ptrdiff_t(expand),binary[expanded].left);
        }
        const std::uint32_t wideIndex=std::uint32_t(out.nodes.size());
        out.nodes.emplace_back();
        GpuBvh4Node wideNode{};
        wideNode.childCount=std::uint32_t(children.size());
        for(std::uint32_t i=0;i<wideNode.childCount;++i) {
            const auto& child=binary[children[i]];
            auto& dst=wideNode.children[i];
            dst.minX=child.bounds.lo.x;dst.minY=child.bounds.lo.y;dst.minZ=child.bounds.lo.z;
            dst.maxX=child.bounds.hi.x;dst.maxY=child.bounds.hi.y;dst.maxZ=child.bounds.hi.z;
            if(child.triangleCount>0) {
                dst.reference=child.firstTriangle;
                dst.triangleCount=child.triangleCount;
            } else {
                dst.reference=emitWide(children[i]);
                dst.triangleCount=0;
            }
        }
        out.nodes[wideIndex]=wideNode;
        return wideIndex;
    };
    emitWide(0);
    return out;
}

} // namespace lumatrace::render
