#include "scene_bvh.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
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
    void add(const BuildBounds& b){if(b.lo.x<=b.hi.x && b.lo.y<=b.hi.y && b.lo.z<=b.hi.z){add(b.lo);add(b.hi);}}
    float area() const {
        const float x=std::max(0.f,hi.x-lo.x), y=std::max(0.f,hi.y-lo.y), z=std::max(0.f,hi.z-lo.z);
        return 2.f*(x*y+y*z+z*x);
    }
};

} // namespace

SceneBvh buildSceneBvh(const scene::Scene& source, std::uint32_t leafSize) {
    SceneBvh out;
    out.triangles.reserve(size_t(source.triangleCount));
    std::vector<Vec3> triangleCentroids;
    triangleCentroids.reserve(size_t(source.triangleCount));
    for(const auto& primitive:source.primitives) {
        std::vector<Vec3> transformedPositions(primitive.vertices.size());
        std::vector<Vec3> transformedNormals(primitive.vertices.size());
        for(size_t i=0;i<primitive.vertices.size();++i){
            transformedPositions[i]=point(primitive.worldTransform,primitive.vertices[i].position);
            transformedNormals[i]=normal(primitive.worldTransform,primitive.vertices[i].normal);
        }
        const auto& idx=primitive.indices;
        for(size_t k=0;k+2<idx.size();k+=3) {
            const auto& a=primitive.vertices[idx[k]];
            const auto& b=primitive.vertices[idx[k+1]];
            const auto& c=primitive.vertices[idx[k+2]];
            GpuTriangle t{};
            t.p0=transformedPositions[idx[k]];
            t.p1=transformedPositions[idx[k+1]];
            t.p2=transformedPositions[idx[k+2]];
            t.n0=transformedNormals[idx[k]];
            t.n1=transformedNormals[idx[k+1]];
            t.n2=transformedNormals[idx[k+2]];
            t.uv0=a.uv0; t.uv1=b.uv0; t.uv2=c.uv0;
            t.material=std::uint32_t(std::max(0,primitive.material));
            t.triangleId=std::uint32_t(out.triangles.size());
            out.triangles.push_back(t);
            triangleCentroids.push_back(centroid(t));
        }
    }
    if(out.triangles.empty()) return out;
    out.triangleOrder.resize(out.triangles.size());
    std::iota(out.triangleOrder.begin(),out.triangleOrder.end(),0u);

    // Build a binary SAH tree first; it preserves the familiar 12-bin SAH
    // partitioning, then collapse its upper levels into four-way GPU nodes.
    struct BinaryNode {
        BuildBounds bounds;
        std::uint32_t left=0, right=0, first=0, count=0, primitiveCount=0;
        bool leaf=false;
    };
    std::vector<BinaryNode> binary;
    // SAH splits can produce more internal nodes than the leaf-size lower bound predicts.
    // Reserve closer to the observed node count to avoid a costly vector growth mid-build,
    // while capping pathological small-leaf requests at the full-binary-tree upper bound.
    const std::size_t estimatedNodes=out.triangles.size()*3u/std::max(1u,leafSize);
    binary.reserve(std::min(out.triangles.size()*2u,estimatedNodes));
    constexpr int BinCount=12;
    const auto binFor=[&](float value,float lo,float hi) {
        const float normalized=(value-lo)/(hi-lo);
        return std::min(BinCount-1,std::max(0,int(normalized*float(BinCount))));
    };
    const auto buildBinary=[&](auto&& self,std::uint32_t first,std::uint32_t count)->std::uint32_t {
        BuildBounds bounds,centroidBounds;
        for(std::uint32_t i=first;i<first+count;++i) {
            const auto& t=out.triangles[out.triangleOrder[i]];
            bounds.add(t.p0);bounds.add(t.p1);bounds.add(t.p2);
            centroidBounds.add(triangleCentroids[out.triangleOrder[i]]);
        }
        const std::uint32_t at=std::uint32_t(binary.size());
        binary.emplace_back();
        binary[at].bounds=bounds;
        binary[at].primitiveCount=count;
        if(count<=std::max(1u,leafSize)) {
            binary[at].leaf=true;binary[at].first=first;binary[at].count=count;
            return at;
        }

        const float centroidLo[3]={centroidBounds.lo.x,centroidBounds.lo.y,centroidBounds.lo.z};
        const float centroidHi[3]={centroidBounds.hi.x,centroidBounds.hi.y,centroidBounds.hi.z};
        float bestCost=std::numeric_limits<float>::infinity();
        int bestAxis=-1,bestSplit=-1;
        struct Bin { BuildBounds bounds; std::uint32_t count=0; };
        for(int axis=0;axis<3;++axis) {
            const float lo=centroidLo[axis],hi=centroidHi[axis];
            if(hi-lo<=1.e-8f) continue;
            Bin bins[BinCount];
            const auto coord=[&](std::uint32_t id) {
                const auto c=triangleCentroids[id];return axis==0?c.x:axis==1?c.y:c.z;
            };
            for(std::uint32_t i=first;i<first+count;++i) {
                const auto& t=out.triangles[out.triangleOrder[i]];
                Bin& bin=bins[binFor(coord(out.triangleOrder[i]),lo,hi)];
                ++bin.count;bin.bounds.add(t.p0);bin.bounds.add(t.p1);bin.bounds.add(t.p2);
            }
            BuildBounds leftBounds[BinCount-1],rightBounds[BinCount-1];
            std::uint32_t leftCounts[BinCount-1]={},rightCounts[BinCount-1]={};
            BuildBounds runningLeft,runningRight;
            std::uint32_t runningLeftCount=0,runningRightCount=0;
            for(int i=0;i<BinCount-1;++i) {
                runningLeft.add(bins[i].bounds);runningLeftCount+=bins[i].count;
                leftBounds[i]=runningLeft;leftCounts[i]=runningLeftCount;
                const int r=BinCount-1-i;
                runningRight.add(bins[r].bounds);runningRightCount+=bins[r].count;
                rightBounds[r-1]=runningRight;rightCounts[r-1]=runningRightCount;
            }
            for(int split=0;split<BinCount-1;++split) {
                if(leftCounts[split]==0||rightCounts[split]==0) continue;
                const float cost=leftBounds[split].area()*float(leftCounts[split])
                                +rightBounds[split].area()*float(rightCounts[split]);
                if(cost<bestCost){bestCost=cost;bestAxis=axis;bestSplit=split;}
            }
        }

        std::uint32_t mid=first;
        if(bestAxis>=0) {
            const float lo=centroidLo[bestAxis],hi=centroidHi[bestAxis];
            auto begin=out.triangleOrder.begin()+first;
            auto finish=out.triangleOrder.begin()+first+count;
            auto pivot=std::partition(begin,finish,[&](std::uint32_t id) {
                const auto c=triangleCentroids[id];
                const float value=bestAxis==0?c.x:bestAxis==1?c.y:c.z;
                return binFor(value,lo,hi)<=bestSplit;
            });
            mid=std::uint32_t(pivot-out.triangleOrder.begin());
        }
        if(mid==first||mid==first+count) {
            const Vec3 e{centroidBounds.hi.x-centroidBounds.lo.x,
                         centroidBounds.hi.y-centroidBounds.lo.y,
                         centroidBounds.hi.z-centroidBounds.lo.z};
            int axis=e.y>e.x?1:0;if((axis==0?e.x:e.y)<e.z)axis=2;
            mid=first+count/2;
            const auto coord=[axis,&triangleCentroids](std::uint32_t id) {
                const auto c=triangleCentroids[id];return axis==0?c.x:axis==1?c.y:c.z;
            };
            std::nth_element(out.triangleOrder.begin()+first,out.triangleOrder.begin()+mid,
                out.triangleOrder.begin()+first+count,[&](std::uint32_t l,std::uint32_t r) {
                    return coord(l)<coord(r);
                });
        }
        const auto left=self(self,first,mid-first);
        const auto right=self(self,mid,first+count-mid);
        binary[at].left=left;binary[at].right=right;
        return at;
    };
    const std::uint32_t binaryRoot=buildBinary(buildBinary,0,std::uint32_t(out.triangles.size()));

    // Collapse binary nodes only when the split lowers the estimated SAH
    // primitive cost. This keeps the four-way nodes from introducing
    // extra overlap merely to fill all four child slots.
    const auto emitWide=[&](auto&& self,std::uint32_t sourceIndex)->std::uint32_t {
        const std::uint32_t at=std::uint32_t(out.nodes.size());
        out.nodes.emplace_back();
        const BinaryNode& sourceNode=binary[sourceIndex];
        auto& node=out.nodes[at];
        node.minX=sourceNode.bounds.lo.x;node.minY=sourceNode.bounds.lo.y;node.minZ=sourceNode.bounds.lo.z;
        node.maxX=sourceNode.bounds.hi.x;node.maxY=sourceNode.bounds.hi.y;node.maxZ=sourceNode.bounds.hi.z;
        if(sourceNode.leaf) {
            node.firstTriangle=sourceNode.first;node.triangleCount=sourceNode.count;
            return at;
        }
        std::vector<std::uint32_t> children{sourceNode.left,sourceNode.right};
        while(children.size()<4) {
            auto candidate=children.end();
            float bestBenefit=0.f;
            for(auto it=children.begin();it!=children.end();++it) {
                const auto& child=binary[*it];
                if(child.leaf) continue;
                const auto& left=binary[child.left];
                const auto& right=binary[child.right];
                const float before=child.bounds.area()*float(child.primitiveCount);
                const float after=left.bounds.area()*float(left.primitiveCount)
                                 +right.bounds.area()*float(right.primitiveCount);
                const float benefit=before-after;
                if(benefit>bestBenefit) { bestBenefit=benefit;candidate=it; }
            }
            if(candidate==children.end()) break;
            const auto expanded=*candidate;
            const auto position=std::size_t(candidate-children.begin());
            children.erase(candidate);
            children.insert(children.begin()+position,{binary[expanded].left,binary[expanded].right});
        }
        std::uint32_t childIndices[4]={};
        for(std::size_t i=0;i<children.size();++i) childIndices[i]=self(self,children[i]);
        GpuBvhNode& target=out.nodes[at];
        target.child0=childIndices[0];target.child1=childIndices[1];
        target.child2=childIndices[2];target.child3=childIndices[3];
        target.childCount=std::uint32_t(children.size());
        return at;
    };
    // Reserve the measured BVH4 node fraction before recursive emission.
    // This avoids repeated vector growth on the Porsche scene without changing node order.
    out.nodes.reserve(binary.size()*3u/4u);
    emitWide(emitWide,binaryRoot);
    const auto& root=out.nodes.front();
    out.bounds.minimum={root.minX,root.minY,root.minZ};
    out.bounds.maximum={root.maxX,root.maxY,root.maxZ};
    out.bounds.valid=true;
    return out;
}
} // namespace lumatrace::render
