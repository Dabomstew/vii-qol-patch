#include "../src/neptasm.cpp"
#include <cstdio>
#include <cstdlib>
namespace vii {
void Log(const char*,...) noexcept {}
int Option(const Context&,const wchar_t*,const wchar_t*,int fallback){return fallback;}
bool ReplaceImport(const Context&,uintptr_t,void*,void*){return false;}
}
using namespace vii;
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(false)
static HRESULT STDMETHODCALLTYPE Description(IDXGISwapChain*,DXGI_SWAP_CHAIN_DESC* desc){
    *desc={};desc->BufferDesc.Width=1920;desc->BufferDesc.Height=1080;return S_OK;
}
static void MinimalComHooks(){
    State graphics;state=&graphics;
    void* deviceTable[64],*contextTable[160],*swapTable[18];
    for(auto& slot:deviceTable)slot=reinterpret_cast<void*>(1);
    for(auto& slot:contextTable)slot=reinterpret_cast<void*>(2);
    for(auto& slot:swapTable)slot=reinterpret_cast<void*>(3);
    swapTable[12]=reinterpret_cast<void*>(Description);
    struct Object {void** table;};Object device{deviceTable},context{contextTable},swap{swapTable};
    CHECK(InstallComHooks(reinterpret_cast<IDXGISwapChain*>(&swap),reinterpret_cast<ID3D11Device*>(&device),reinterpret_cast<ID3D11DeviceContext*>(&context)));
    CHECK(swap.table==swapTable&&device.table==deviceTable&&context.table==contextTable);
    for(unsigned i=0;i<64;++i)CHECK(i==5?device.table[i]!=reinterpret_cast<void*>(1):device.table[i]==reinterpret_cast<void*>(1));
    for(unsigned i=0;i<160;++i)CHECK(i==44?context.table[i]!=reinterpret_cast<void*>(2):context.table[i]==reinterpret_cast<void*>(2));
    CHECK(InstallComHooks(reinterpret_cast<IDXGISwapChain*>(&swap),reinterpret_cast<ID3D11Device*>(&device),reinterpret_cast<ID3D11DeviceContext*>(&context)));
    CHECK(originalTexture==reinterpret_cast<CreateTextureFn>(1)&&originalViewport==reinterpret_cast<ViewportFn>(2));
    contextTable[44]=nullptr;
    CHECK(!InstallComHooks(reinterpret_cast<IDXGISwapChain*>(&swap),reinterpret_cast<ID3D11Device*>(&device),reinterpret_cast<ID3D11DeviceContext*>(&context)));
    CHECK(!graphics.comActive);state=nullptr;
}
static void RealD3DScaling(){
    ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)));
    State graphics;graphics.context.game=GetModuleHandleW(nullptr);CHECK(ReadImageRange(graphics));
    graphics.settings.resolution=true;graphics.settings.scale=0.5f;state=&graphics;
    void* swapTable[18]{};swapTable[12]=reinterpret_cast<void*>(Description);
    struct Object {void** table;} swap{swapTable};
    CHECK(InstallComHooks(reinterpret_cast<IDXGISwapChain*>(&swap),device,context));
    D3D11_TEXTURE2D_DESC desc{};desc.Width=1920;desc.Height=1080;desc.MipLevels=desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    ID3D11Texture2D* texture=nullptr;CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&texture))&&texture);
    D3D11_TEXTURE2D_DESC actual{};texture->GetDesc(&actual);CHECK(actual.Width==960&&actual.Height==540);
    graphics.comActive=false;ID3D11Texture2D* fallback=nullptr;
    CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&fallback))&&fallback);
    fallback->GetDesc(&actual);CHECK(actual.Width==1920&&actual.Height==1080);fallback->Release();graphics.comActive=true;
    D3D11_VIEWPORT viewport{0,0,1920,1080,0,1};context->RSSetViewports(1,&viewport);
    UINT count=1;context->RSGetViewports(&count,&viewport);CHECK(count==1&&viewport.Width==960&&viewport.Height==540);
    ID3D11RenderTargetView* view=nullptr;CHECK(SUCCEEDED(device->CreateRenderTargetView(texture,nullptr,&view)));
    const float clear[]={0,0,0,1};context->ClearRenderTargetView(view,clear);context->Flush();
    desc.Format=DXGI_FORMAT_D32_FLOAT;desc.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    ID3D11Texture2D* depth=nullptr;CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&depth))&&depth);
    depth->GetDesc(&actual);CHECK(actual.Width==960&&actual.Height==540);
    ID3D11DepthStencilView* depthView=nullptr;CHECK(SUCCEEDED(device->CreateDepthStencilView(depth,nullptr,&depthView)));
    context->ClearDepthStencilView(depthView,D3D11_CLEAR_DEPTH,1.0f,0);depthView->Release();depth->Release();
    CHECK(SetFixedRenderSize(graphics.settings,1280,720));
    graphics.settings.fitWindow=true;graphics.settings.windowControl=true;
    graphics.settings.windowWidth=2560;graphics.settings.windowHeight=1440;
    graphics.outputWidth=1366;graphics.outputHeight=768;Recompute();
    CHECK(graphics.renderWidth==1280&&graphics.renderHeight==720);
    ID3D11Texture2D* fixed=nullptr;CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&fixed))&&fixed);
    fixed->GetDesc(&actual);CHECK(actual.Width==1280&&actual.Height==720);fixed->Release();
    viewport={0,0,1920,1080,0,1};context->RSSetViewports(1,&viewport);
    count=1;context->RSGetViewports(&count,&viewport);CHECK(viewport.Width==1280&&viewport.Height==720);
    graphics.outputWidth=3840;graphics.outputHeight=2160;Recompute();
    CHECK(graphics.renderWidth==1280&&graphics.renderHeight==720);
    CHECK(ReplaceMethod(graphics.deviceVtable+5,reinterpret_cast<void*>(CreateTexture),reinterpret_cast<void*>(originalTexture)));
    CHECK(ReplaceMethod(graphics.contextVtable+44,reinterpret_cast<void*>(Viewport),reinterpret_cast<void*>(originalViewport)));
    view->Release();texture->Release();context->Release();device->Release();state=nullptr;
}
static void FixedSizeRules(){
    State graphics;state=&graphics;graphics.settings.resolution=true;graphics.settings.scale=0.5f;
    graphics.outputWidth=1600;graphics.outputHeight=900;
    for(const auto pair:{std::pair<int,int>{0,0},{1280,0},{0,720},{-1,720},{1280,-1},{16385,720},{1280,16385}}){
        CHECK(!SetFixedRenderSize(graphics.settings,pair.first,pair.second));Recompute();
        CHECK(graphics.renderWidth==800&&graphics.renderHeight==450);
    }
    CHECK(SetFixedRenderSize(graphics.settings,16384,16384));Recompute();
    CHECK(graphics.renderWidth==16384&&graphics.renderHeight==16384);
    CHECK(SetFixedRenderSize(graphics.settings,1280,720));graphics.settings.resolution=false;Recompute();
    CHECK(graphics.renderWidth==1920&&graphics.renderHeight==1080); // Dimensions alone never enable it.
    state=nullptr;
}
static DWORD cameraException=0;
static uintptr_t cameraFaultAddress=0;
static int CameraFault(EXCEPTION_POINTERS* error){
    cameraException=error->ExceptionRecord->ExceptionCode;
    if(error->ExceptionRecord->NumberParameters>=2)
        cameraFaultAddress=error->ExceptionRecord->ExceptionInformation[1];
    return EXCEPTION_EXECUTE_HANDLER;
}
static bool ExecuteCameraLoad(void* instruction,void* object,float* output,bool maximum){
    cameraException=0;cameraFaultAddress=0;
    __try {
        if(maximum){
            __asm {
                push esi
                mov esi,object
                mov eax,instruction
                call eax
                mov eax,output
                movss [eax],xmm2
                pop esi
            }
        }else{
            __asm {
                push esi
                mov esi,object
                mov eax,instruction
                call eax
                mov eax,output
                movss [eax],xmm5
                pop esi
            }
        }
        return true;
    } __except(CameraFault(GetExceptionInformation())) {return false;}
}
static void CameraOperands(){
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2c2000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    CHECK(image);Context context{};context.game=reinterpret_cast<HMODULE>(image);
    const unsigned char maximum[]={0xf3,0x0f,0x10,0x96,0xb4,0x01,0x00,0x00,0xc3};
    const unsigned char minimum[]={0xf3,0x0f,0x10,0xae,0xb8,0x01,0x00,0x00,0xc3};
    std::memcpy(image+0x2c1478,maximum,sizeof(maximum));
    std::memcpy(image+0x2c1483,minimum,sizeof(minimum));
    DWORD old=0;CHECK(VirtualProtect(image,0x2c2000,PAGE_EXECUTE_READ,&old));
    float object[112]{};object[0x1b4/4]=45.0f;object[0x1b8/4]=135.0f;
    Settings settings;CHECK(ApplyStatic(context,settings));
    float value=0;CHECK(ExecuteCameraLoad(image+0x2c1478,object,&value,true)&&value==45.0f);
    CHECK(ExecuteCameraLoad(image+0x2c1483,object,&value,false)&&value==135.0f);
    settings.camera=true;CHECK(ApplyStatic(context,settings));
    bool valid=true;
    for(bool maximumLoad:{true,false}){
        const bool executed=ExecuteCameraLoad(image+(maximumLoad?0x2c1478:0x2c1483),object,&value,maximumLoad);
        if(!executed)std::fprintf(stderr,"CameraUnlock exception=%08lx address=%08lx operand=%s\n",cameraException,static_cast<unsigned long>(cameraFaultAddress),maximumLoad?"maximum":"minimum");
        valid=executed&&value==(maximumLoad?270.0f:90.0f)&&valid;
    }
    CHECK(valid);
    CHECK(!ApplyStatic(context,settings)); // Already-patched signature must be refused.
    CHECK(VirtualFree(image,0,MEM_RELEASE));
}
static unsigned ExecuteFrameGate(void* instruction,void* object){
    unsigned result=0;
    __asm {
        push esi
        push edi
        mov esi,object
        mov eax,instruction
        call eax
        mov result,eax
        pop edi
        pop esi
    }
    return result;
}
static unsigned capturedWidth=0,capturedHeight=0;
static HRESULT WINAPI CaptureCreation(IDXGIAdapter*,D3D_DRIVER_TYPE,HMODULE,UINT,const D3D_FEATURE_LEVEL*,UINT,UINT,
    const DXGI_SWAP_CHAIN_DESC* desc,IDXGISwapChain**,ID3D11Device**,D3D_FEATURE_LEVEL*,ID3D11DeviceContext**){
    capturedWidth=desc->BufferDesc.Width;capturedHeight=desc->BufferDesc.Height;return E_FAIL;
}
static void StaticAndWindowControls(){
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x6f1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    CHECK(image);Context context{};context.game=reinterpret_cast<HMODULE>(image);
    const uintptr_t base=reinterpret_cast<uintptr_t>(image);
    const unsigned char frame[]={0x8b,0x7e,0x20,0x85,0xff,0x0f,0x84,0x8a,0,0,0};
    const unsigned char fallthrough[]={0xb8,1,0,0,0,0xc3},taken[]={0xb8,2,0,0,0,0xc3};
    const unsigned char projection[]={0xf3,0x0f,0x10,0x86,0x80,0,0,0};
    std::memcpy(image+0x404b4c,frame,sizeof(frame));
    std::memcpy(image+0x404b4c+sizeof(frame),fallthrough,sizeof(fallthrough));
    std::memcpy(image+0x404b4c+sizeof(frame)+0x8a,taken,sizeof(taken));
    const uint32_t fpsPointer=uint32_t(base+0x4f2b80);std::memcpy(image+0x3b20a8,&fpsPointer,4);
    const float stockFps=60.0f;std::memcpy(image+0x4f2b80,&stockFps,4);
    image[0x46bb12]=0x52;image[0x46bb13]=0x50;
    std::memcpy(image+0x1081de,projection,sizeof(projection));
    const uint32_t mapBits=0x3fe38e39;std::memcpy(image+0x5bdc94,&mapBits,4);
    const unsigned widths[]={640,720,800,1024,1152,1280,1360,1366,1600,1920};
    for(unsigned i=0;i<10;++i){const unsigned height=widths[i]*9/16;
        std::memcpy(image+0x6f04c8+i*8,&widths[i],4);std::memcpy(image+0x6f04cc+i*8,&height,4);}
    DWORD old=0;CHECK(VirtualProtect(image,0x6f1000,PAGE_EXECUTE_READ,&old));
    unsigned object[12]{};object[8]=1;CHECK(ExecuteFrameGate(image+0x404b4c,object)==1);
    object[8]=0;CHECK(ExecuteFrameGate(image+0x404b4c,object)==2);object[8]=1;
    Settings settings;CHECK(ApplyStatic(context,settings));
    CHECK(ExecuteFrameGate(image+0x404b4c,object)==1);CHECK(WindowTableMatches(base));
    settings.fps=true;CHECK(ApplyStatic(context,settings));
    CHECK(ExecuteFrameGate(image+0x404b4c,object)==2);
    CHECK(*reinterpret_cast<float*>(*reinterpret_cast<uint32_t*>(image+0x3b20a8))==1024.0f);
    CHECK(image[0x46bb12]==0xeb&&image[0x46bb13]==5);
    CHECK(!ApplyStatic(context,settings));settings.fps=false;
    settings.windowControl=true;settings.windowWidth=1280;settings.windowHeight=720;
    CHECK(ApplyStatic(context,settings));
    for(unsigned i=0;i<10;++i){CHECK(*reinterpret_cast<uint32_t*>(image+0x6f04c8+i*8)==1280);
        CHECK(*reinterpret_cast<uint32_t*>(image+0x6f04cc+i*8)==720);}
    State graphics;graphics.context=context;graphics.settings.fitWindow=true;state=&graphics;
    originalCreate=CaptureCreation;aspectPatched=false;
    RECT rect{0,0,1280,540};CHECK(AdjustWindowRect(&rect,WS_OVERLAPPEDWINDOW,FALSE));
    HWND window=CreateWindowW(L"STATIC",L"FitWindow fixture",WS_OVERLAPPEDWINDOW,0,0,
        rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    CHECK(window);DXGI_SWAP_CHAIN_DESC desc{};desc.BufferDesc.Width=1920;desc.BufferDesc.Height=1080;desc.OutputWindow=window;
    CHECK(CreateDevice(nullptr,D3D_DRIVER_TYPE_NULL,nullptr,0,nullptr,0,0,&desc,nullptr,nullptr,nullptr,nullptr)==E_FAIL);
    CHECK(capturedWidth==1280&&capturedHeight==540);CHECK(aspectPatched);
    const auto operand=*reinterpret_cast<uint32_t*>(image+0x1081e2);
    CHECK(image[0x1081e1]==5&&std::abs(*reinterpret_cast<float*>(operand)-1280.0f/540)<0.00001f);
    CHECK(std::abs(*reinterpret_cast<float*>(image+0x5bdc94)-1280.0f/540)<0.00001f);
    CHECK(desc.BufferDesc.Width==1920&&desc.BufferDesc.Height==1080); // Caller input stays unchanged.
    CHECK(DestroyWindow(window));
    RECT initial{0,0,1366,768};CHECK(AdjustWindowRect(&initial,WS_OVERLAPPEDWINDOW,FALSE));
    window=CreateWindowW(L"STATIC",L"Custom window fixture",WS_OVERLAPPEDWINDOW,0,0,
        initial.right-initial.left,initial.bottom-initial.top,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    CHECK(window);graphics.settings.fitWindow=false;graphics.settings.windowControl=true;
    graphics.settings.windowWidth=1280;graphics.settings.windowHeight=720;
    desc.BufferDesc.Width=1366;desc.BufferDesc.Height=768;desc.OutputWindow=window;desc.Windowed=TRUE;
    CHECK(CreateDevice(nullptr,D3D_DRIVER_TYPE_NULL,nullptr,0,nullptr,0,0,&desc,nullptr,nullptr,nullptr,nullptr)==E_FAIL);
    CHECK(capturedWidth==1280&&capturedHeight==720);
    RECT client{};CHECK(GetClientRect(window,&client));CHECK(client.right==1280&&client.bottom==720);
    CHECK(desc.BufferDesc.Width==1366&&desc.BufferDesc.Height==768);
    CHECK(DestroyWindow(window));graphics.settings.windowControl=false;
    graphics.settings.fitWindow=false;graphics.settings.ultrawide=true;
    desc.BufferDesc.Width=1600;desc.BufferDesc.Height=900;
    CHECK(CreateDevice(nullptr,D3D_DRIVER_TYPE_NULL,nullptr,0,nullptr,0,0,&desc,nullptr,nullptr,nullptr,nullptr)==E_FAIL);
    CHECK(capturedWidth==1600&&capturedHeight==900); // Ultrawide never changes output dimensions.
    state=nullptr;originalCreate=nullptr;aspectPatched=false;fpsReturn=0;
    CHECK(VirtualFree(image,0,MEM_RELEASE));
}
int main() {
    State image;CHECK(!ReadImageRange(image));
    auto* bytes=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    CHECK(bytes);image.context.game=reinterpret_cast<HMODULE>(bytes);
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(bytes);
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(bytes+256);
    dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=256;
    nt->Signature=IMAGE_NT_SIGNATURE;nt->OptionalHeader.SizeOfImage=4096;
    CHECK(ReadImageRange(image));state=&image;
    const auto begin=reinterpret_cast<uintptr_t>(bytes);
    CHECK(!InGame(begin-1)&&InGame(begin)&&InGame(begin+4095)&&!InGame(begin+4096));
    dos->e_magic=0;CHECK(InGame(begin+128)); // Hot path uses validated range.
    CHECK(!ReadImageRange(image));dos->e_magic=IMAGE_DOS_SIGNATURE;
    nt->Signature=0;CHECK(!ReadImageRange(image));nt->Signature=IMAGE_NT_SIGNATURE;
    nt->OptionalHeader.SizeOfImage=0;CHECK(!ReadImageRange(image));
    dos->e_lfanew=0x7fffffff;CHECK(!ReadImageRange(image)); // Guarded malformed read.
    state=nullptr;CHECK(!InGame(begin));CHECK(VirtualFree(bytes,0,MEM_RELEASE));
    CameraOperands();StaticAndWindowControls();MinimalComHooks();FixedSizeRules();RealD3DScaling();
    std::printf("PASS %u checks: cached caller range, preserved complete tables and real D3D texture/viewport scaling\n",checks);
}
