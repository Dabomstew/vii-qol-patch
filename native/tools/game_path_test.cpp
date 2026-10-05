#include "game_path.hpp"
#include <cassert>
#include <cstdio>
using namespace vii;
int main(){
    const std::wstring game=L"C:\\Games\\NeptuniaVII";
    assert(ResolveGamePath(game,L"",L"vii-speedrun-patch\\cache")==L"C:\\Games\\NeptuniaVII\\vii-speedrun-patch\\cache");
    assert(ResolveGamePath(game,L"relative\\cache",L"fallback")==L"C:\\Games\\NeptuniaVII\\relative\\cache");
    assert(ResolveGamePath(game,L"D:\\cache",L"fallback")==L"D:\\cache");
    assert(ResolveGamePath(game,L"\\\\server\\share\\cache",L"fallback")==L"\\\\server\\share\\cache");
    puts("Game-relative, drive-absolute, UNC and empty-default paths passed");
}
