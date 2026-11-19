#include "scene_bvh.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace lumatrace::render {
namespace {
using scene::Matrix4;
using scene::Vec3;

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
    // Cofactors form inverse-transpose(M), which is the normal transform.
    Vec3 n{A*v.x+B*v.y+C*v.z,D*v.x+E*v.y+F*v.z,G*v.x+H*v.y+I*v.z};
    const float l=std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);
    if(l>1.e-12f){n.x/=l;n.y/=l;n.z/=l;}
    return n;
}

Vec3 centroid(const GpuTriangle& t) {
    return {(t.p0.x+t.p1.x+t.p2.x)/3.f,(t.p0.y+t.p1.y+t.p2.y)/3.f,(t.p0.z+t.p1.z+t.p2.z)/3.f};
}

struct BuildBounds {
    Vec3 lo{1.e30f,1.e30f,1.e30f};
    Vec3 hi{-1.e30f,-1.e30f,-1.e30f};
    void add(Vec3 p){lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};
                   hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};}
};

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
    out.triangleOrder.resize(out.triangles.size());
    std::iota(out.triangleOrder.begin(),out.triangleOrder.end(),0u);
    const auto build=[&](auto&& self,std::uint32_t first,std::uint32_t count)->std::uint32_t {
        BuildBounds b,cb;
        for(std::uint32_t i=first;i<first+count;++i){
            const auto& t=out.triangles[out.triangleOrder[i]];
            b.add(t.p0);b.add(t.p1);b.add(t.p2);cb.add(centroid(t));
        }
        GpuBvhNode node{};
        node.minX=b.lo.x;node.minY=b.lo.y;node.minZ=b.lo.z;
        node.maxX=b.hi.x;node.maxY=b.hi.y;node.maxZ=b.hi.z;
        const std::uint32_t at=std::uint32_t(out.nodes.size());
        out.nodes.push_back(node);
        if(count<=std::max(1u,leafSize)){
            out.nodes[at].firstTriangle=first;
            out.nodes[at].triangleCount=count;
            return at;
        }
        const Vec3 extent{cb.hi.x-cb.lo.x,cb.hi.y-cb.lo.y,cb.hi.z-cb.lo.z};
        int axis=extent.y>extent.x?1:0;
        if((axis==0?extent.x:extent.y)<extent.z)axis=2;
        const auto coord=[axis](const GpuTriangle& t){const auto c=centroid(t);return axis==0?c.x:axis==1?c.y:c.z;};
        const std::uint32_t mid=first+count/2;
        std::nth_element(out.triangleOrder.begin()+first,out.triangleOrder.begin()+mid,
            out.triangleOrder.begin()+first+count,[&](std::uint32_t l,std::uint32_t r){
                return coord(out.triangles[l])<coord(out.triangles[r]);
            });
        const auto left=self(self,first,mid-first);
        const auto right=self(self,mid,first+count-mid);
        out.nodes[at].leftChild=left;out.nodes[at].rightChild=right;
        out.nodes[at].firstTriangle=0;out.nodes[at].triangleCount=0;
        return at;
    };
    build(build,0,std::uint32_t(out.triangles.size()));
    const auto& root=out.nodes.front();
    out.bounds.minimum={root.minX,root.minY,root.minZ};
    out.bounds.maximum={root.maxX,root.maxY,root.maxZ};
    out.bounds.valid=true;
    return out;
}

} // namespace lumatrace::render
