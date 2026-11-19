#include "d3d11_scene_renderer.h"

#ifdef _WIN32
#include "render_config.h"

#include <QCoreApplication>
#include <QImage>
#include <QColor>
#include <QFileInfo>
#include <QDir>

#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <climits>
#include <iterator>
#include <optional>
#include <vector>

namespace lumatrace::render {
namespace {
using Microsoft::WRL::ComPtr;
constexpr UINT kWidth = 960, kHeight = 540;
constexpr UINT kMaxComputeMaterialTextures = 64;
constexpr UINT kComputeTextureSize = 512;
constexpr float kPi = 3.14159265358979323846f;

struct GpuVertex { float p[3], n[3], uv[2], tangent[4], color[4]; };
static_assert(sizeof(GpuVertex) == 64);
struct alignas(16) RasterCamera { float vp[16]; float world[16]; float normalWorld[16]; float camera[4]; };
struct alignas(16) PrepassCamera { float vp[16]; float world[16]; std::uint32_t triangleBase, alphaMode; float alphaCutoff, baseAlpha; };
struct alignas(16) MaterialConstants { float baseColor[4], surface[4], emissive[4], details[4]; std::uint32_t triangleBase, padding[3]; };
struct alignas(16) LightConstants { std::uint32_t width, height, triangleCount, samples; float camera[3], epsilon; float light[3], intensity; float lightColor[3], angularRadius; };
static_assert(sizeof(LightConstants) == 64);
struct alignas(16) FocusedSpotConstants {
    float position[3], intensity;
    float direction[3], range;
    float color[3], innerCos;
    float outerCos, padding[3];
};
static_assert(sizeof(FocusedSpotConstants) == 64);
struct GpuMaterial { float baseColor[4]; float metallic, roughness; float pad[2]; };

template<class T> bool makeBuffer(ID3D11Device* d, UINT bind, UINT misc, UINT stride, const T* data, size_t count, ComPtr<ID3D11Buffer>& out) {
    if (count == 0 || count > (size_t(UINT_MAX) / sizeof(T))) return false;
    D3D11_BUFFER_DESC desc{}; desc.ByteWidth = UINT(sizeof(T) * count); desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = bind; desc.MiscFlags = misc; desc.StructureByteStride = stride;
    D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem = data;
    return SUCCEEDED(d->CreateBuffer(&desc, &initial, &out));
}

bool makeConstantBuffer(ID3D11Device* d, UINT bytes, ComPtr<ID3D11Buffer>& out) {
    D3D11_BUFFER_DESC desc{}; desc.ByteWidth = (bytes + 15u) & ~15u; desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER; desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    return SUCCEEDED(d->CreateBuffer(&desc, nullptr, &out));
}

template<class T> bool updateBuffer(ID3D11DeviceContext* c, ID3D11Buffer* b, const T& value) {
    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(c->Map(b, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) return false;
    std::memcpy(map.pData, &value, sizeof(T)); c->Unmap(b, 0); return true;
}

bool compileShader(const wchar_t* file, const char* entry, const char* profile, ComPtr<ID3DBlob>& blob, QString& error,
                   diagnostics::RunLogger* logger) {
    std::optional<diagnostics::RunLogger::Scope> timing;
    const QString shaderName=QString::fromWCharArray(file)+QStringLiteral(" · ")+QString::fromLatin1(entry);
    if(logger)timing.emplace(logger->scope(QStringLiteral("D3DCompile · ")+shaderName));
    const QString path = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("shaders/") + QString::fromWCharArray(file));
    ComPtr<ID3DBlob> errors;
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    const HRESULT hr = D3DCompileFromFile(reinterpret_cast<const wchar_t*>(path.utf16()), nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE, entry, profile, flags, 0, &blob, &errors);
    if (FAILED(hr)) {
        const QString detail = errors ? QString::fromUtf8(static_cast<const char*>(errors->GetBufferPointer()), int(errors->GetBufferSize())) : QString();
        error = QStringLiteral("Shader %1 : %2").arg(QFileInfo(path).fileName(), detail);
        return false;
    }
    return true;
}

std::array<float,16> identity() { return {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}; }
std::array<float,16> normalMatrix(const scene::Matrix4& m) {
    const float a=m[0],b=m[1],c=m[2],d=m[4],e=m[5],f=m[6],g=m[8],h=m[9],i=m[10];
    const float A=e*i-f*h,B=c*h-b*i,C=b*f-c*e,D=f*g-d*i,E=a*i-c*g,F=c*d-a*f,G=d*h-e*g,H=b*g-a*h,I=a*e-b*d;
    const float det=a*A+b*D+c*G; auto out=identity();
    if(std::abs(det)<1.e-12f)return out;
    // The HLSL row-vector World matrix is transpose(glTF World). Normals
    // therefore use transpose(inverse(HLSL World)) = inverse(glTF World).
    const float inv=1.f/det;out[0]=A*inv;out[1]=D*inv;out[2]=G*inv;out[4]=B*inv;out[5]=E*inv;out[6]=H*inv;out[8]=C*inv;out[9]=F*inv;out[10]=I*inv;return out;
}
void normalize(float& x,float& y,float& z) { const float l=std::sqrt(x*x+y*y+z*z); if(l>1.e-8f){x/=l;y/=l;z/=l;} }

// Row-vector view-projection, Direct3D depth range [0,1].
std::array<float,16> cameraMatrix(const scene::Bounds& bounds, float camera[3]) {
    scene::Vec3 center = bounds.valid ? scene::Vec3{(bounds.minimum.x+bounds.maximum.x)*.5f,(bounds.minimum.y+bounds.maximum.y)*.5f,(bounds.minimum.z+bounds.maximum.z)*.5f} : scene::Vec3{};
    const float dx=bounds.maximum.x-bounds.minimum.x, dy=bounds.maximum.y-bounds.minimum.y, dz=bounds.maximum.z-bounds.minimum.z;
    const float radius=std::max(1.f,std::sqrt(dx*dx+dy*dy+dz*dz)*.5f);
    // Lift the subject slightly in frame and move the camera closer for a larger car.
    center.y -= radius*.12f;
    float fx=.85f, fy=.48f, fz=1.f; normalize(fx,fy,fz);
    camera[0]=center.x+fx*radius*1.5f; camera[1]=center.y+fy*radius*1.5f; camera[2]=center.z+fz*radius*1.5f;
    float zx=center.x-camera[0], zy=center.y-camera[1], zz=center.z-camera[2]; normalize(zx,zy,zz);
    float xx=zz, xy=0.f, xz=-zx; normalize(xx,xy,xz);
    const float yx=zy*xz-zz*xy, yy=zz*xx-zx*xz, yz=zx*xy-zy*xx;
    const float tx=-(camera[0]*xx+camera[1]*xy+camera[2]*xz);
    const float ty=-(camera[0]*yx+camera[1]*yy+camera[2]*yz);
    const float tz=-(camera[0]*zx+camera[1]*zy+camera[2]*zz);
    const float nearZ=std::max(.01f,radius*.02f), farZ=radius*8.f+nearZ;
    const float f=1.f/std::tan(60.f*kPi/360.f), aspect=float(kWidth)/float(kHeight);
    const float p[16]={f/aspect,0,0,0, 0,f,0,0, 0,0,farZ/(farZ-nearZ),1, 0,0,-nearZ*farZ/(farZ-nearZ),0};
    const float v[16]={xx,yx,zx,0, xy,yy,zy,0, xz,yz,zz,0, tx,ty,tz,1};
    std::array<float,16> out{};
    for(int r=0;r<4;++r)for(int c=0;c<4;++c)for(int k=0;k<4;++k)out[r*4+c]+=v[r*4+k]*p[k*4+c];
    return out;
}
FocusedSpotConstants focusedSpotForBounds(const scene::Bounds& bounds) {
    const scene::Vec3 center = bounds.valid ? scene::Vec3{(bounds.minimum.x+bounds.maximum.x)*.5f,(bounds.minimum.y+bounds.maximum.y)*.5f,(bounds.minimum.z+bounds.maximum.z)*.5f} : scene::Vec3{};
    const float dx=bounds.maximum.x-bounds.minimum.x, dy=bounds.maximum.y-bounds.minimum.y, dz=bounds.maximum.z-bounds.minimum.z;
    const float radius=std::max(1.f,std::sqrt(dx*dx+dy*dy+dz*dz)*.5f);
    float cameraX=.85f,cameraY=.48f,cameraZ=1.f;normalize(cameraX,cameraY,cameraZ);
    float rightX=-cameraZ,rightZ=cameraX;
    const float rightLength=std::sqrt(rightX*rightX+rightZ*rightZ);
    if(rightLength>1.e-8f){rightX/=rightLength;rightZ/=rightLength;}
    // Place a soft studio spot above the camera-facing hood/front quarter.
    const float targetX=center.x+rightX*radius*.28f,targetY=center.y,targetZ=center.z+rightZ*radius*.28f;
    FocusedSpotConstants spot{};
    spot.position[0]=center.x+cameraX*radius*.90f;
    spot.position[1]=center.y+radius*1.45f;
    spot.position[2]=center.z+cameraZ*radius*.90f;
    spot.direction[0]=targetX-spot.position[0];
    spot.direction[1]=targetY-spot.position[1];
    spot.direction[2]=targetZ-spot.position[2];
    normalize(spot.direction[0],spot.direction[1],spot.direction[2]);
    spot.intensity=7.0f;
    spot.range=radius*3.6f;
    spot.color[0]=1.0f;spot.color[1]=.93f;spot.color[2]=.82f;
    spot.innerCos=.88f;spot.outerCos=.62f;
    return spot;
}
}

struct D3D11SceneRenderer::Impl {
    struct Draw { ComPtr<ID3D11Buffer> vb, ib; UINT indexCount=0, triangleBase=0; int material=0; };
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context; ComPtr<IDXGISwapChain1> swap;
    ComPtr<ID3D11RenderTargetView> backRtv, idRtv; ComPtr<ID3D11Texture2D> backBuffer, imageStaging, idTexture, depthTexture;
    ComPtr<ID3D11ShaderResourceView> idSrv; ComPtr<ID3D11UnorderedAccessView> idUav, visibleUav, radianceUav;
    ComPtr<ID3D11DepthStencilView> depthDsv; ComPtr<ID3D11DepthStencilState> depthWrite, depthRead;
    ComPtr<ID3D11BlendState> alphaBlend; ComPtr<ID3D11RasterizerState> rasterState, rasterDoubleSided;
    ComPtr<ID3D11VertexShader> preVs, rasterVs; ComPtr<ID3D11PixelShader> prePs, rasterPs;
    ComPtr<ID3D11ComputeShader> collectCs, shadeCs; ComPtr<ID3D11InputLayout> inputLayout;
    ComPtr<ID3D11SamplerState> sampler; ComPtr<ID3D11Buffer> preCameraCb, rasterCameraCb, materialCb, lightCb, focusedSpotCb;
    ComPtr<ID3D11Buffer> bvhBuffer, triangleBuffer, orderBuffer, materialBuffer, visibleBuffer, radianceBuffer;
    ComPtr<ID3D11ShaderResourceView> bvhSrv, triangleSrv, orderSrv, materialSrv, visibleSrv, radianceSrv;
    std::vector<Draw> draws; std::vector<std::array<ComPtr<ID3D11ShaderResourceView>,5>> materialTextures;
    ComPtr<ID3D11ShaderResourceView> computeBaseColorArray;
    std::array<ComPtr<ID3D11ShaderResourceView>,5> fallbackTextures;
    std::array<float,16> vp{}; float camera[3]{}; UINT triCount=0; UINT samples=1; bool ready=false, imageExportPending=true, timingNextFrame=true;
    diagnostics::RunLogger* logger=nullptr; QString imageOutputPath;

    bool makeTexture(const QImage& source, ComPtr<ID3D11ShaderResourceView>& out, bool srgb=false) {
        QImage image=source.convertToFormat(QImage::Format_RGBA8888);
        if(image.isNull()) return false;
        D3D11_TEXTURE2D_DESC td{}; td.Width=UINT(image.width()); td.Height=UINT(image.height()); td.MipLevels=1; td.ArraySize=1;
        td.Format=srgb?DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count=1; td.Usage=D3D11_USAGE_IMMUTABLE; td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init{}; init.pSysMem=image.constBits(); init.SysMemPitch=UINT(image.bytesPerLine());
        ComPtr<ID3D11Texture2D> texture;
        if(FAILED(device->CreateTexture2D(&td,&init,&texture))) return false;
        return SUCCEEDED(device->CreateShaderResourceView(texture.Get(),nullptr,&out));
    }

    bool makeComputeBaseColorArray(const scene::Scene& scene, QString& error) {
        const UINT layers = UINT(std::max(1, int(scene.materials.size())));
        std::vector<QImage> images;
        std::vector<D3D11_SUBRESOURCE_DATA> initial(layers);
        images.reserve(layers);
        for (UINT layer = 0; layer < layers; ++layer) {
            QImage source;
            if (layer < UINT(scene.materials.size())) {
                const int textureIndex = scene.materials[int(layer)].baseColorTexture.texture;
                if (textureIndex >= 0 && textureIndex < scene.textures.size())
                    source = scene.textures[textureIndex].image;
            }
            if (source.isNull()) {
                source = QImage(1, 1, QImage::Format_RGBA8888);
                source.fill(QColor(Qt::white));
            }
            images.push_back(source.convertToFormat(QImage::Format_RGBA8888)
                                  .scaled(int(kComputeTextureSize), int(kComputeTextureSize),
                                          Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
            auto& data = initial[layer];
            data.pSysMem = images.back().constBits();
            data.SysMemPitch = UINT(images.back().bytesPerLine());
            data.SysMemSlicePitch = UINT(images.back().sizeInBytes());
        }

        D3D11_TEXTURE2D_DESC textureDesc{};
        textureDesc.Width = kComputeTextureSize;
        textureDesc.Height = kComputeTextureSize;
        textureDesc.MipLevels = 1;
        textureDesc.ArraySize = layers;
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.Usage = D3D11_USAGE_IMMUTABLE;
        textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> texture;
        if (FAILED(device->CreateTexture2D(&textureDesc, initial.data(), &texture))) {
            error = QStringLiteral("D3D11 : création du tableau de textures compute impossible.");
            return false;
        }
        D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
        viewDesc.Format = textureDesc.Format;
        viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        viewDesc.Texture2DArray.MostDetailedMip = 0;
        viewDesc.Texture2DArray.MipLevels = 1;
        viewDesc.Texture2DArray.FirstArraySlice = 0;
        viewDesc.Texture2DArray.ArraySize = layers;
        if (FAILED(device->CreateShaderResourceView(texture.Get(), &viewDesc, &computeBaseColorArray))) {
            error = QStringLiteral("D3D11 : vue du tableau de textures compute impossible.");
            return false;
        }
        if (logger) logger->note(QStringLiteral("Tableau baseColor compute : %1 couches de %2×%2.")
                                     .arg(layers).arg(kComputeTextureSize));
        return true;
    }

    bool createSceneResources(const scene::Scene& scene,const SceneBvh& bvh,QString& error) {
        if(bvh.triangles.empty() || bvh.nodes.empty()) { error=QStringLiteral("La scène ne contient aucun triangle exploitable."); return false; }
        if(scene.materials.size() > int(kMaxComputeMaterialTextures)) {
            error=QStringLiteral("Le raytracing de couleur accepte au maximum %1 matériaux.").arg(kMaxComputeMaterialTextures);
            return false;
        }
        triCount=UINT(bvh.triangles.size());
        std::optional<diagnostics::RunLogger::Scope> uploadTiming;
        if(logger)uploadTiming.emplace(logger->scope(QStringLiteral("GPU upload · triangles et BVH")));
        if(!makeBuffer(device.Get(),D3D11_BIND_SHADER_RESOURCE,D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,sizeof(GpuTriangle),bvh.triangles.data(),bvh.triangles.size(),triangleBuffer)
        || !makeBuffer(device.Get(),D3D11_BIND_SHADER_RESOURCE,D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,sizeof(GpuBvhNode),bvh.nodes.data(),bvh.nodes.size(),bvhBuffer)
        || !makeBuffer(device.Get(),D3D11_BIND_SHADER_RESOURCE,D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,sizeof(std::uint32_t),bvh.triangleOrder.data(),bvh.triangleOrder.size(),orderBuffer)) {
            error=QStringLiteral("D3D11 : upload des triangles/BVH impossible."); return false;
        }
        auto makeSrv=[&](ID3D11Buffer* b,UINT n,ComPtr<ID3D11ShaderResourceView>& srv){ D3D11_SHADER_RESOURCE_VIEW_DESC d{}; d.Format=DXGI_FORMAT_UNKNOWN; d.ViewDimension=D3D11_SRV_DIMENSION_BUFFER; d.Buffer.NumElements=n; return SUCCEEDED(device->CreateShaderResourceView(b,&d,&srv)); };
        if(!makeSrv(triangleBuffer.Get(),triCount,triangleSrv)||!makeSrv(bvhBuffer.Get(),UINT(bvh.nodes.size()),bvhSrv)||!makeSrv(orderBuffer.Get(),UINT(bvh.triangleOrder.size()),orderSrv)){ error=QStringLiteral("D3D11 : création des SRV BVH impossible."); return false; }
        if(logger)logger->note(QStringLiteral("Buffers BVH : %1 nœuds, %2 triangles.").arg(bvh.nodes.size()).arg(bvh.triangles.size()));
        uploadTiming.reset();

        if(logger)uploadTiming.emplace(logger->scope(QStringLiteral("GPU upload · matériaux et textures")));
        std::vector<GpuMaterial> gm; gm.reserve(size_t(scene.materials.size()));
        for(const auto& m:scene.materials) gm.push_back({{m.baseColor.x,m.baseColor.y,m.baseColor.z,m.baseColor.w},m.metallic,m.roughness,{0,0}});
        if(gm.empty()) gm.push_back({{.7f,.7f,.7f,1.f},0.f,.7f,{0,0}});
        if(!makeBuffer(device.Get(),D3D11_BIND_SHADER_RESOURCE,D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,sizeof(GpuMaterial),gm.data(),gm.size(),materialBuffer)
        ||!makeSrv(materialBuffer.Get(),UINT(gm.size()),materialSrv)){error=QStringLiteral("D3D11 : upload des matériaux impossible.");return false;}
        std::vector<UINT> zeros(triCount,0); std::vector<std::array<float,4>> lightZeros(triCount,{0,0,0,0});
        if(!makeBuffer(device.Get(),D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_SHADER_RESOURCE,D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,sizeof(UINT),zeros.data(),zeros.size(),visibleBuffer)
        ||!makeBuffer(device.Get(),D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_SHADER_RESOURCE,D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,16,lightZeros.data(),lightZeros.size(),radianceBuffer)){error=QStringLiteral("D3D11 : buffers de travail compute impossibles.");return false;}
        D3D11_UNORDERED_ACCESS_VIEW_DESC ud{}; ud.Format=DXGI_FORMAT_UNKNOWN; ud.ViewDimension=D3D11_UAV_DIMENSION_BUFFER; ud.Buffer.NumElements=triCount;
        if(FAILED(device->CreateUnorderedAccessView(visibleBuffer.Get(),&ud,&visibleUav))||FAILED(device->CreateUnorderedAccessView(radianceBuffer.Get(),&ud,&radianceUav))
        ||!makeSrv(visibleBuffer.Get(),triCount,visibleSrv)||!makeSrv(radianceBuffer.Get(),triCount,radianceSrv)){error=QStringLiteral("D3D11 : vues compute impossibles.");return false;}

        const UINT white=0xffffffffu, normal=0xff8080ffu, black=0xff000000u;
        auto solid=[&](UINT pixel,ComPtr<ID3D11ShaderResourceView>& srv,bool srgb){ QImage image(1,1,QImage::Format_RGBA8888); image.fill(QColor::fromRgba(pixel)); return makeTexture(image,srv,srgb); };
        solid(white,fallbackTextures[0],true); solid(normal,fallbackTextures[1],false); solid(white,fallbackTextures[2],false); solid(white,fallbackTextures[3],false); solid(black,fallbackTextures[4],true);
        materialTextures.resize(size_t(scene.materials.size()));
        for(size_t mi=0;mi<materialTextures.size();++mi){
            const auto& m=scene.materials[int(mi)]; const scene::TextureReference* refs[]={&m.baseColorTexture,&m.normalTexture,&m.metallicRoughnessTexture,&m.occlusionTexture,&m.emissiveTexture};
            for(size_t slot=0;slot<5;++slot){ const int ti=refs[slot]->texture;
                if(ti>=0&&ti<scene.textures.size()&&!scene.textures[ti].image.isNull()) { if(!makeTexture(scene.textures[ti].image,materialTextures[mi][slot],slot==0||slot==4)) materialTextures[mi][slot]=fallbackTextures[slot]; }
                else materialTextures[mi][slot]=fallbackTextures[slot];
            }
        }
        if (!makeComputeBaseColorArray(scene,error)) return false;
        if(logger)logger->note(QStringLiteral("Matériaux GPU : %1 ; textures de scène : %2.").arg(gm.size()).arg(scene.textures.size()));
        uploadTiming.reset();
        if(logger)uploadTiming.emplace(logger->scope(QStringLiteral("GPU upload · vertex/index buffers par primitive")));
        UINT base=0;
        for(const auto& p:scene.primitives){
            std::vector<GpuVertex> v; v.reserve(p.vertices.size());
            for(const auto& a:p.vertices) v.push_back({{a.position.x,a.position.y,a.position.z},{a.normal.x,a.normal.y,a.normal.z},{a.uv0.x,a.uv0.y},{a.tangent.x,a.tangent.y,a.tangent.z,a.tangent.w},{a.color.x,a.color.y,a.color.z,a.color.w}});
            Draw d; d.indexCount=UINT(p.indices.size()); d.triangleBase=base; d.material=scene.materials.isEmpty()?-1:std::clamp(p.material,0,int(scene.materials.size())-1);
            D3D11_BUFFER_DESC vd{}; vd.ByteWidth=UINT(v.size()*sizeof(GpuVertex)); vd.Usage=D3D11_USAGE_IMMUTABLE; vd.BindFlags=D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA vi{}; vi.pSysMem=v.data();
            D3D11_BUFFER_DESC id{}; id.ByteWidth=UINT(p.indices.size()*sizeof(UINT)); id.Usage=D3D11_USAGE_IMMUTABLE; id.BindFlags=D3D11_BIND_INDEX_BUFFER;
            D3D11_SUBRESOURCE_DATA ii{}; ii.pSysMem=p.indices.data();
            if(!v.empty()&&!p.indices.empty()) {
                if(FAILED(device->CreateBuffer(&vd,&vi,&d.vb))||FAILED(device->CreateBuffer(&id,&ii,&d.ib))) {
                    error=QStringLiteral("D3D11 : allocation de géométrie impossible pour %1.").arg(p.meshName);return false;
                }
            }
            draws.push_back(std::move(d));
            base+=UINT(p.indices.size()/3);
        }
        if(logger)logger->note(QStringLiteral("Buffers géométrie créés pour %1 primitives.").arg(draws.size()));
        return true;
    }

    bool createTargets(HWND hwnd,QString& error) {
        ComPtr<IDXGIDevice> xd; ComPtr<IDXGIAdapter> adapter; ComPtr<IDXGIFactory2> factory;
        if(FAILED(device.As(&xd))||FAILED(xd->GetAdapter(&adapter))||FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))){error=QStringLiteral("D3D11 : accès à DXGI impossible.");return false;}
        DXGI_SWAP_CHAIN_DESC1 sd{}; sd.Width=kWidth;sd.Height=kHeight;sd.Format=DXGI_FORMAT_B8G8R8A8_UNORM;sd.SampleDesc.Count=1;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.BufferCount=2;sd.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;sd.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
        if(FAILED(factory->CreateSwapChainForHwnd(device.Get(),hwnd,&sd,nullptr,nullptr,&swap))){error=QStringLiteral("DXGI : création swapchain impossible.");return false;}
        if(FAILED(swap->GetBuffer(0,IID_PPV_ARGS(&backBuffer)))||FAILED(device->CreateRenderTargetView(backBuffer.Get(),nullptr,&backRtv))){error=QStringLiteral("D3D11 : backbuffer indisponible.");return false;}
        D3D11_TEXTURE2D_DESC imageDesc{};backBuffer->GetDesc(&imageDesc);imageDesc.Usage=D3D11_USAGE_STAGING;imageDesc.BindFlags=0;imageDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;imageDesc.MiscFlags=0;
        if(FAILED(device->CreateTexture2D(&imageDesc,nullptr,&imageStaging))){error=QStringLiteral("D3D11 : staging de capture image impossible.");return false;}
        D3D11_TEXTURE2D_DESC td{};td.Width=kWidth;td.Height=kHeight;td.MipLevels=1;td.ArraySize=1;td.Format=DXGI_FORMAT_R32_UINT;td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
        if(FAILED(device->CreateTexture2D(&td,nullptr,&idTexture))||FAILED(device->CreateRenderTargetView(idTexture.Get(),nullptr,&idRtv))||FAILED(device->CreateShaderResourceView(idTexture.Get(),nullptr,&idSrv))||FAILED(device->CreateUnorderedAccessView(idTexture.Get(),nullptr,&idUav))){error=QStringLiteral("D3D11 : cible ID triangle impossible.");return false;}
        td.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;td.BindFlags=D3D11_BIND_DEPTH_STENCIL;
        if(FAILED(device->CreateTexture2D(&td,nullptr,&depthTexture))||FAILED(device->CreateDepthStencilView(depthTexture.Get(),nullptr,&depthDsv))){error=QStringLiteral("D3D11 : cible de profondeur impossible.");return false;}
        return true;
    }

    bool createPipeline(QString& error) {
        ComPtr<ID3DBlob> vs,ps,cs; D3D11_INPUT_ELEMENT_DESC il[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},{"TANGENT",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,32,D3D11_INPUT_PER_VERTEX_DATA,0},{"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,48,D3D11_INPUT_PER_VERTEX_DATA,0}};
        if(!compileShader(L"z_prepass.hlsl","VSMain","vs_5_0",vs,error,logger)||FAILED(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&preVs))||FAILED(device->CreateInputLayout(il,5,vs->GetBufferPointer(),vs->GetBufferSize(),&inputLayout))){if(error.isEmpty())error=QStringLiteral("D3D11 : création du vertex shader Z impossible.");return false;}
        if(!compileShader(L"z_prepass.hlsl","PSMain","ps_5_0",ps,error,logger)||FAILED(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&prePs))){if(error.isEmpty())error=QStringLiteral("D3D11 : création pixel shader Z impossible.");return false;}
        if(!compileShader(L"raster_pbr.hlsl","VSMain","vs_5_0",vs,error,logger)||FAILED(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&rasterVs))||FAILED(device->CreateInputLayout(il,5,vs->GetBufferPointer(),vs->GetBufferSize(),&inputLayout))){if(error.isEmpty())error=QStringLiteral("D3D11 : création du vertex shader raster impossible.");return false;}
        if(!compileShader(L"raster_pbr.hlsl","PSMain","ps_5_0",ps,error,logger)||FAILED(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&rasterPs))){if(error.isEmpty())error=QStringLiteral("D3D11 : création pixel shader raster impossible.");return false;}
        if(!compileShader(L"triangle_lighting.hlsl","CSCollectVisible","cs_5_0",cs,error,logger)||FAILED(device->CreateComputeShader(cs->GetBufferPointer(),cs->GetBufferSize(),nullptr,&collectCs))){if(error.isEmpty())error=QStringLiteral("D3D11 : création compute collect impossible.");return false;}
        if(!compileShader(L"triangle_lighting.hlsl","CSShadeVisibleTriangles","cs_5_0",cs,error,logger)||FAILED(device->CreateComputeShader(cs->GetBufferPointer(),cs->GetBufferSize(),nullptr,&shadeCs))){if(error.isEmpty())error=QStringLiteral("D3D11 : création compute lumière impossible.");return false;}
        if(!makeConstantBuffer(device.Get(),sizeof(PrepassCamera),preCameraCb)||!makeConstantBuffer(device.Get(),sizeof(RasterCamera),rasterCameraCb)||!makeConstantBuffer(device.Get(),sizeof(MaterialConstants),materialCb)||!makeConstantBuffer(device.Get(),sizeof(LightConstants),lightCb)||!makeConstantBuffer(device.Get(),sizeof(FocusedSpotConstants),focusedSpotCb)){error=QStringLiteral("D3D11 : allocations de constantes impossibles.");return false;}
        D3D11_SAMPLER_DESC ss{};ss.Filter=D3D11_FILTER_ANISOTROPIC;ss.MaxAnisotropy=4;ss.AddressU=ss.AddressV=ss.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;ss.MaxLOD=D3D11_FLOAT32_MAX;
        if(FAILED(device->CreateSamplerState(&ss,&sampler))){error=QStringLiteral("D3D11 : sampler impossible.");return false;}
        D3D11_DEPTH_STENCIL_DESC ds{};ds.DepthEnable=TRUE;ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;ds.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;ds.StencilEnable=FALSE;
        if(FAILED(device->CreateDepthStencilState(&ds,&depthWrite))){error=QStringLiteral("D3D11 : état profondeur impossible.");return false;}
        ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
        if(FAILED(device->CreateDepthStencilState(&ds,&depthRead))){error=QStringLiteral("D3D11 : état lecture profondeur impossible.");return false;}
        D3D11_RASTERIZER_DESC rs{};rs.FillMode=D3D11_FILL_SOLID;rs.CullMode=D3D11_CULL_BACK;rs.DepthClipEnable=TRUE;
        if(FAILED(device->CreateRasterizerState(&rs,&rasterState))){error=QStringLiteral("D3D11 : rasterizer impossible.");return false;}
        rs.CullMode=D3D11_CULL_NONE;
        if(FAILED(device->CreateRasterizerState(&rs,&rasterDoubleSided))){error=QStringLiteral("D3D11 : rasterizer double face impossible.");return false;}
        D3D11_BLEND_DESC bd{};bd.RenderTarget[0].BlendEnable=TRUE;bd.RenderTarget[0].SrcBlend=D3D11_BLEND_SRC_ALPHA;bd.RenderTarget[0].DestBlend=D3D11_BLEND_INV_SRC_ALPHA;bd.RenderTarget[0].BlendOp=D3D11_BLEND_OP_ADD;bd.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ONE;bd.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;bd.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD;bd.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        if(FAILED(device->CreateBlendState(&bd,&alphaBlend))){error=QStringLiteral("D3D11 : état alpha impossible.");return false;}
        return true;
    }
};

D3D11SceneRenderer::D3D11SceneRenderer():impl_(std::make_unique<Impl>()){}
D3D11SceneRenderer::~D3D11SceneRenderer()=default;
void D3D11SceneRenderer::setSamplesPerTriangle(std::uint32_t samples){const auto next=std::clamp(samples,1u,4u);if(next!=impl_->samples){impl_->samples=next;impl_->imageExportPending=true;impl_->timingNextFrame=true;if(impl_->logger)impl_->logger->note(QStringLiteral("Échantillons lumineux/triangle : %1 ; passe chronométrée et nouvel export PNG demandés.").arg(impl_->samples));}}

bool D3D11SceneRenderer::initialize(HWND hwnd,const scene::Scene& scene,const SceneBvh& bvh,
                                    diagnostics::RunLogger& logger,QString imageOutputPath,QString& status){
    auto initialization=logger.scope(QStringLiteral("D3D11SceneRenderer::initialize"));
    auto& x=*impl_; const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0};D3D_FEATURE_LEVEL got{};
    x.logger=&logger;x.imageOutputPath=std::move(imageOutputPath);
    HRESULT hr=S_OK;bool warp=false;
    {auto phase=logger.scope(QStringLiteral("D3D11CreateDevice · matériel puis WARP"));hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels,UINT(std::size(levels)),D3D11_SDK_VERSION,&x.device,&got,&x.context);if(FAILED(hr)){warp=true;hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels,UINT(std::size(levels)),D3D11_SDK_VERSION,&x.device,&got,&x.context);}}
    if(FAILED(hr)){status=QStringLiteral("Aucun périphérique D3D11 matériel ou WARP disponible.");return false;}
    {auto phase=logger.scope(QStringLiteral("swapchain, backbuffer, profondeur, ID triangle et staging PNG"));if(!x.createTargets(hwnd,status))return false;}
    {auto phase=logger.scope(QStringLiteral("HLSL · compilation des vertex/pixel/compute shaders et états D3D"));if(!x.createPipeline(status))return false;}
    {auto phase=logger.scope(QStringLiteral("ressources de scène et buffers GPU"));if(!x.createSceneResources(scene,bvh,status))return false;}
    x.vp=cameraMatrix(scene.bounds,x.camera);x.ready=true;
    ComPtr<IDXGIDevice> xd;ComPtr<IDXGIAdapter> ad;DXGI_ADAPTER_DESC desc{};QString gpu=warp?QStringLiteral("WARP logiciel"):QStringLiteral("GPU matériel");
    if(!warp&&SUCCEEDED(x.device.As(&xd))&&SUCCEEDED(xd->GetAdapter(&ad))&&SUCCEEDED(ad->GetDesc(&desc)))gpu=QString::fromWCharArray(desc.Description).trimmed();
    const unsigned major=unsigned(got)>>12,minor=(unsigned(got)>>8)&0xf;
    status=QStringLiteral("%1 · D3D %2.%3 · 960×540 · Z-prepass + BVH · %4 triangles · 1 échantillon/triangle")
        .arg(gpu).arg(major).arg(minor).arg(x.triCount);
    return true;
}

void D3D11SceneRenderer::render(const scene::Scene& scene,const SceneBvh& bvh){
    Q_UNUSED(bvh);
    auto& x=*impl_;if(!x.ready)return;
    const bool logFrame=x.timingNextFrame;x.timingNextFrame=false;
    std::optional<diagnostics::RunLogger::Scope> frameTiming;
    if(logFrame&&x.logger)frameTiming.emplace(x.logger->scope(QStringLiteral("frame · rendu complet du profil courant")));
    const float clear[4]={.035f,.045f,.06f,1.f};const UINT noId[4]={0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu};const UINT zero[4]={0,0,0,0};
    std::optional<diagnostics::RunLogger::Scope> passTiming;
    if(logFrame&&x.logger)passTiming.emplace(x.logger->scope(QStringLiteral("Z-prepass · profondeur et ID triangle visible")));
    x.context->ClearUnorderedAccessViewUint(x.idUav.Get(),noId);ID3D11RenderTargetView* idTarget=x.idRtv.Get();x.context->OMSetRenderTargets(1,&idTarget,x.depthDsv.Get());x.context->ClearDepthStencilView(x.depthDsv.Get(),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,1.f,0);
    D3D11_VIEWPORT vp{0,0,float(kWidth),float(kHeight),0,1};x.context->RSSetViewports(1,&vp);x.context->RSSetState(x.rasterState.Get());x.context->OMSetDepthStencilState(x.depthWrite.Get(),0);x.context->IASetInputLayout(x.inputLayout.Get());x.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);x.context->VSSetShader(x.preVs.Get(),nullptr,0);x.context->PSSetShader(x.prePs.Get(),nullptr,0);
    for(size_t i=0;i<scene.primitives.size()&&i<x.draws.size();++i){const auto& p=scene.primitives[int(i)];const auto& d=x.draws[i];if(!d.vb||!d.ib)continue;const int mi=d.material;const scene::Material* m=mi>=0?&scene.materials[mi]:nullptr;const UINT am=m?(m->alphaMode==QStringLiteral("MASK")?1u:m->alphaMode==QStringLiteral("BLEND")?2u:0u):0u;x.context->RSSetState(m&&m->doubleSided?x.rasterDoubleSided.Get():x.rasterState.Get());
        PrepassCamera c{};std::copy(x.vp.begin(),x.vp.end(),c.vp);std::copy(p.worldTransform.begin(),p.worldTransform.end(),c.world);c.triangleBase=d.triangleBase;c.alphaMode=am;c.alphaCutoff=m?m->alphaCutoff:.5f;c.baseAlpha=m?m->baseColor.w:1.f;updateBuffer(x.context.Get(),x.preCameraCb.Get(),c);ID3D11Buffer* cb=x.preCameraCb.Get();x.context->VSSetConstantBuffers(0,1,&cb);x.context->PSSetConstantBuffers(0,1,&cb);
        ID3D11ShaderResourceView* tex=x.fallbackTextures[0].Get();if(m&&mi<int(x.materialTextures.size()))tex=x.materialTextures[size_t(mi)][0].Get();x.context->PSSetShaderResources(0,1,&tex);ID3D11SamplerState* sm=x.sampler.Get();x.context->PSSetSamplers(0,1,&sm);
        UINT stride=sizeof(GpuVertex),offset=0;ID3D11Buffer* vb=d.vb.Get();x.context->IASetVertexBuffers(0,1,&vb,&stride,&offset);x.context->IASetIndexBuffer(d.ib.Get(),DXGI_FORMAT_R32_UINT,0);if(am!=2)x.context->DrawIndexed(d.indexCount,0,0);
    }
    ID3D11RenderTargetView* none=nullptr;x.context->OMSetRenderTargets(0,&none,nullptr);
    passTiming.reset();
    if(logFrame&&x.logger)passTiming.emplace(x.logger->scope(QStringLiteral("HLSL compute · marquage des triangles visibles")));
    x.context->ClearUnorderedAccessViewUint(x.visibleUav.Get(),zero);
    // Studio key light: compact apparent source, warm white, upper-left/front.
    // The reduced angular radius makes the cast shadows more directional.
    LightConstants lc{};lc.width=kWidth;lc.height=kHeight;lc.triangleCount=x.triCount;lc.samples=x.samples;lc.camera[0]=x.camera[0];lc.camera[1]=x.camera[1];lc.camera[2]=x.camera[2];lc.epsilon=std::max(1.e-4f,std::sqrt(std::pow(scene.bounds.maximum.x-scene.bounds.minimum.x,2.f)+std::pow(scene.bounds.maximum.y-scene.bounds.minimum.y,2.f)+std::pow(scene.bounds.maximum.z-scene.bounds.minimum.z,2.f))*1.e-5f);lc.light[0]=-.55f;lc.light[1]=.74f;lc.light[2]=.38f;normalize(lc.light[0],lc.light[1],lc.light[2]);lc.intensity=2.1f;lc.lightColor[0]=1.f;lc.lightColor[1]=.91f;lc.lightColor[2]=.78f;lc.angularRadius=.035f;updateBuffer(x.context.Get(),x.lightCb.Get(),lc);ID3D11Buffer* light=x.lightCb.Get();x.context->CSSetConstantBuffers(0,1,&light);
    ID3D11ShaderResourceView* ids=x.idSrv.Get();x.context->CSSetShaderResources(0,1,&ids);ID3D11UnorderedAccessView* vis=x.visibleUav.Get();x.context->CSSetUnorderedAccessViews(0,1,&vis,nullptr);x.context->CSSetShader(x.collectCs.Get(),nullptr,0);x.context->Dispatch((kWidth+7)/8,(kHeight+7)/8,1);
    passTiming.reset();
    if(logFrame&&x.logger)passTiming.emplace(x.logger->scope(QStringLiteral("HLSL compute · rayons d'ombre/réflexion et parcours BVH")));
    ID3D11ShaderResourceView* nullSrv=nullptr;ID3D11UnorderedAccessView* nullUav=nullptr;x.context->CSSetShaderResources(0,1,&nullSrv);x.context->CSSetUnorderedAccessViews(0,1,&nullUav,nullptr);
    ID3D11ShaderResourceView* computeSrvs[5]={x.triangleSrv.Get(),x.orderSrv.Get(),x.bvhSrv.Get(),x.materialSrv.Get(),x.visibleSrv.Get()};
    x.context->CSSetShaderResources(1,5,computeSrvs);
    ID3D11ShaderResourceView* computeBaseColor=x.computeBaseColorArray.Get();
    x.context->CSSetShaderResources(6,1,&computeBaseColor);
    ID3D11SamplerState* computeSampler=x.sampler.Get();x.context->CSSetSamplers(0,1,&computeSampler);
    ID3D11UnorderedAccessView* outputs[2]={nullptr,x.radianceUav.Get()};x.context->CSSetUnorderedAccessViews(0,2,outputs,nullptr);x.context->CSSetShader(x.shadeCs.Get(),nullptr,0);x.context->Dispatch((x.triCount+63)/64,1,1);
    ID3D11SamplerState* nullSampler=nullptr;x.context->CSSetShader(nullptr,nullptr,0);std::array<ID3D11ShaderResourceView*,6> nullComputeSrvs{};x.context->CSSetShaderResources(1,UINT(nullComputeSrvs.size()),nullComputeSrvs.data());x.context->CSSetSamplers(0,1,&nullSampler);x.context->CSSetUnorderedAccessViews(0,2,std::array<ID3D11UnorderedAccessView*,2>{}.data(),nullptr);
    passTiming.reset();
    if(logFrame&&x.logger)passTiming.emplace(x.logger->scope(QStringLiteral("raster Direct3D · textures, normales, alpha et affichage")));
    ID3D11RenderTargetView* color=x.backRtv.Get();x.context->OMSetRenderTargets(1,&color,x.depthDsv.Get());x.context->ClearRenderTargetView(x.backRtv.Get(),clear);x.context->OMSetDepthStencilState(x.depthRead.Get(),0);x.context->VSSetShader(x.rasterVs.Get(),nullptr,0);x.context->PSSetShader(x.rasterPs.Get(),nullptr,0);ID3D11ShaderResourceView* lightSrv=x.radianceSrv.Get();x.context->PSSetShaderResources(5,1,&lightSrv);ID3D11Buffer* focusedSpot=x.focusedSpotCb.Get();x.context->PSSetConstantBuffers(2,1,&focusedSpot);ID3D11SamplerState* samp=x.sampler.Get();x.context->PSSetSamplers(0,1,&samp);
    for(size_t i=0;i<scene.primitives.size()&&i<x.draws.size();++i){const auto& p=scene.primitives[int(i)];const auto& d=x.draws[i];if(!d.vb||!d.ib)continue;const int mi=d.material;const scene::Material* m=mi>=0?&scene.materials[mi]:nullptr;x.context->RSSetState(m&&m->doubleSided?x.rasterDoubleSided.Get():x.rasterState.Get());RasterCamera rc{};std::copy(x.vp.begin(),x.vp.end(),rc.vp);std::copy(p.worldTransform.begin(),p.worldTransform.end(),rc.world);const auto nm=normalMatrix(p.worldTransform);std::copy(nm.begin(),nm.end(),rc.normalWorld);rc.camera[0]=x.camera[0];rc.camera[1]=x.camera[1];rc.camera[2]=x.camera[2];rc.camera[3]=1;updateBuffer(x.context.Get(),x.rasterCameraCb.Get(),rc);ID3D11Buffer* cb=x.rasterCameraCb.Get();x.context->VSSetConstantBuffers(0,1,&cb);x.context->PSSetConstantBuffers(0,1,&cb);
        MaterialConstants mc{};mc.baseColor[0]=m?m->baseColor.x:1;mc.baseColor[1]=m?m->baseColor.y:1;mc.baseColor[2]=m?m->baseColor.z:1;mc.baseColor[3]=m?m->baseColor.w:1;mc.surface[0]=m?m->metallic:0;mc.surface[1]=m?m->roughness:.7f;mc.surface[2]=m?m->alphaCutoff:.5f;mc.surface[3]=m?(m->alphaMode==QStringLiteral("MASK")?1.f:m->alphaMode==QStringLiteral("BLEND")?2.f:0.f):0.f;mc.emissive[0]=m?m->emissive.x:0;mc.emissive[1]=m?m->emissive.y:0;mc.emissive[2]=m?m->emissive.z:0;mc.details[0]=m?m->normalScale:1.f;mc.details[1]=m?m->occlusionStrength:1.f;mc.triangleBase=d.triangleBase;updateBuffer(x.context.Get(),x.materialCb.Get(),mc);cb=x.materialCb.Get();x.context->PSSetConstantBuffers(1,1,&cb);
        ID3D11ShaderResourceView* maps[5];for(int slot=0;slot<5;++slot)maps[slot]=m&&mi<int(x.materialTextures.size())?x.materialTextures[size_t(mi)][size_t(slot)].Get():x.fallbackTextures[size_t(slot)].Get();x.context->PSSetShaderResources(0,5,maps);
        UINT stride=sizeof(GpuVertex),offset=0;ID3D11Buffer* vb=d.vb.Get();x.context->IASetVertexBuffers(0,1,&vb,&stride,&offset);x.context->IASetIndexBuffer(d.ib.Get(),DXGI_FORMAT_R32_UINT,0);if(mc.surface[3]==2){const float blend[4]={0,0,0,0};x.context->OMSetBlendState(x.alphaBlend.Get(),blend,0xffffffff);}else x.context->OMSetBlendState(nullptr,nullptr,0xffffffff);x.context->DrawIndexed(d.indexCount,0,0);
    }
    passTiming.reset();
    x.context->OMSetBlendState(nullptr,nullptr,0xffffffff);x.context->OMSetRenderTargets(0,&none,nullptr);x.context->PSSetShaderResources(0,6,std::array<ID3D11ShaderResourceView*,6>{}.data());
    if(x.imageExportPending){
        std::optional<diagnostics::RunLogger::Scope> imageTiming;
        if(x.logger)imageTiming.emplace(x.logger->scope(QStringLiteral("export · capture PNG du rendu final")));
        x.context->CopyResource(x.imageStaging.Get(),x.backBuffer.Get());
        D3D11_MAPPED_SUBRESOURCE map{};
        if(SUCCEEDED(x.context->Map(x.imageStaging.Get(),0,D3D11_MAP_READ,0,&map))){
            QImage image(int(kWidth),int(kHeight),QImage::Format_ARGB32);
            for(UINT y=0;y<kHeight;++y)std::memcpy(image.scanLine(int(y)),static_cast<const char*>(map.pData)+size_t(y)*map.RowPitch,size_t(kWidth)*4);
            x.context->Unmap(x.imageStaging.Get(),0);
            const QFileInfo baseInfo(x.imageOutputPath);
            const QString rayLabel=x.samples==1?QStringLiteral("rayon"):QStringLiteral("rayons");
            const QString samplePath=QDir(baseInfo.path()).filePath(QStringLiteral("%1_%2_%3.%4")
                .arg(baseInfo.completeBaseName()).arg(x.samples).arg(rayLabel,baseInfo.suffix()));
            if(image.save(samplePath, "PNG")){if(x.logger)x.logger->note(QStringLiteral("Image exportée (%1 %2) : ").arg(x.samples).arg(rayLabel)+samplePath);}
            else if(x.logger)x.logger->note(QStringLiteral("ERREUR : l'image PNG n'a pas pu être écrite : ")+samplePath);
        }else if(x.logger)x.logger->note(QStringLiteral("ERREUR : lecture staging impossible pour l'export PNG."));
        x.imageExportPending=false;
    }
    if(logFrame&&x.logger)passTiming.emplace(x.logger->scope(QStringLiteral("Present · VSync")));
    x.swap->Present(1,0);
}
} // namespace lumatrace::render
#endif
