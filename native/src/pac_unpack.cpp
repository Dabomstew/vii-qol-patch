#include "pac_unpack.hpp"
#include <algorithm>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace vii::unpack {
namespace fs=std::filesystem;
namespace {
struct Handle {
    HANDLE h=INVALID_HANDLE_VALUE;
    Handle()=default;explicit Handle(HANDLE value):h(value){}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    ~Handle(){Close();}void Close(){if(h!=INVALID_HANDLE_VALUE){CloseHandle(h);h=INVALID_HANDLE_VALUE;}}
};
[[noreturn]] void Fail(const char* text){throw std::runtime_error(text);}
Handle* Lock(const fs::path& p,std::vector<std::unique_ptr<Handle>>& handles){
    auto file=std::make_unique<Handle>(CreateFileW(loose::Extended(p).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(file->h==INVALID_HANDLE_VALUE)Fail("Cannot lock source PAC for reading; close programs modifying archives");
    auto result=file.get();handles.push_back(std::move(file));return result;
}
bool IsPac(const fs::path& p){auto ext=p.extension().wstring();for(auto& c:ext)if(c>=L'A'&&c<=L'Z')c+=L'a'-L'A';return ext==L".pac";}
void OwnedRoot(const fs::path& root){
    loose::CheckOutputPath(root,{});
    constexpr char owner[]="VII uncompressed asset output v1\n";
    const auto marker=loose::Extended(root/L".vii-unpacked-owner");
    if(fs::exists(marker)){
        loose::CheckOutputPath(root,L".vii-unpacked-owner");std::ifstream file(marker,std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(file)),{});if(text!=owner)Fail("Output folder has an unknown ownership marker");return;
    }
    if(fs::exists(loose::Extended(root))&&!fs::is_empty(loose::Extended(root)))Fail("Choose an empty output folder; existing files are not owned by this unpacker");
    fs::create_directories(loose::Extended(root));Handle file(CreateFileW(marker.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
    DWORD done=0;if(file.h==INVALID_HANDLE_VALUE||!WriteFile(file.h,owner,DWORD(sizeof(owner)-1),&done,nullptr)||done!=sizeof(owner)-1)Fail("Cannot create output ownership marker");
}
struct Temporary {
    fs::path path;Handle file;
    explicit Temporary(const fs::path& destination):path(loose::Extended(destination.wstring()+L".vii-part-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()))),
        file(CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_SEQUENTIAL_SCAN,nullptr)) {
        if(file.h==INVALID_HANDLE_VALUE)Fail("Cannot create temporary output file");
    }
    ~Temporary(){file.Close();if(!path.empty())DeleteFileW(path.c_str());}
    void Publish(const fs::path& destination,bool replace=false){
        if(file.h!=INVALID_HANDLE_VALUE){if(replace&&!FlushFileBuffers(file.h))Fail("Cannot flush output file");file.Close();}
        if(!MoveFileExW(path.c_str(),loose::Extended(destination).c_str(),replace?(MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH):0))Fail("Cannot publish completed output file");path.clear();
    }
};
void CheckCancel(const pac::Cancel& cancel){if(cancel&&cancel())Fail("Cancelled");}
std::string Key(const loose::Manifest& m,const loose::File& f){return m.sources[f.source].group+"/"+f.name;}
}

fs::path FindContents(const fs::path& selected){
    auto source=fs::absolute(selected).lexically_normal();
    if(!fs::is_directory(loose::Extended(source)))Fail("Select a folder containing PAC files");
    if(fs::is_directory(loose::Extended(source/L"CONTENTS")))source/=L"CONTENTS";
    return source;
}

fs::path FindDlc(const fs::path& selected){
    const auto contents=FindContents(selected);
    if(_wcsicmp(contents.filename().c_str(),L"CONTENTS"))return {};
    const auto dlc=contents.parent_path()/L"DLC";
    if(!fs::is_directory(loose::Extended(dlc)))return {};
    for(fs::recursive_directory_iterator it(loose::Extended(dlc)),end;it!=end;++it){
        if(it->is_directory()&&fs::exists(it->path()/L".vii-unpacked-owner")){it.disable_recursion_pending();continue;}
        if(it->is_regular_file()&&IsPac(it->path()))return dlc;
    }
    return {};
}

static Result RunFolder(const fs::path& selected,const fs::path& destination,const Report& report,const pac::Cancel& cancel){
    const auto root=FindContents(selected),output=fs::absolute(destination).lexically_normal();
    if(loose::Extended(root)==loose::Extended(output))Fail("Output must be separate from the source folder");
    std::vector<fs::path> paths;
    for(fs::recursive_directory_iterator it(loose::Extended(root)),end;it!=end;++it){
        CheckCancel(cancel);
        if(it->is_directory()&&(it->path()==loose::Extended(output)||fs::exists(it->path()/L".vii-unpacked-owner"))){it.disable_recursion_pending();continue;}
        if(it->is_regular_file()&&IsPac(it->path()))paths.push_back(it->path());
    }
    std::sort(paths.begin(),paths.end());if(paths.empty())Fail("No PAC files found in the selected folder");
    if(paths.size()>65536)Fail("Too many PAC archives");
    std::vector<std::unique_ptr<Handle>> locks;locks.reserve(paths.size());
    std::vector<pac::Archive> archives;loose::Manifest manifest;Progress progress;std::unordered_set<std::string> unique;
    for(const auto& path:paths){
        CheckCancel(cancel);Lock(path,locks);auto archive=pac::Inspect(path);
        auto source=loose::Identify(path,archive.tableBytes);source.name=pac::NormalizePath(fs::relative(path,loose::Extended(root)).generic_string());source.group=pac::ArchiveNamespace(fs::path(source.name));
        for(const auto& e:archive.entries){if(!unique.insert(source.group+"/"+e.name).second)Fail("Duplicate output path across PAC archives");progress.totalBytes+=e.unpacked;}
        progress.totalFiles+=archive.entries.size();manifest.sources.push_back(std::move(source));archives.push_back(std::move(archive));
    }
    if(progress.totalFiles>1000000)Fail("Too many asset files");
    OwnedRoot(output);loose::CheckOutputPath(output,L"unpack.lock");
    Handle lock(CreateFileW(loose::Extended(output/L"unpack.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(lock.h==INVALID_HANDLE_VALUE)Fail("Another unpacker is using this output folder");
    loose::Manifest old;std::unordered_map<std::string,const loose::File*> reuse;
    const auto manifestPath=output/L"manifest.vii";loose::CheckOutputPath(output,L"manifest.vii");
    if(fs::exists(loose::Extended(manifestPath))){
        old=loose::ReadManifest(manifestPath);bool same=old.sources.size()==manifest.sources.size();
        if(same)for(size_t i=0;i<old.sources.size();++i){const auto& a=old.sources[i];const auto& b=manifest.sources[i];if(a.name!=b.name||a.group!=b.group||a.size!=b.size||a.stamp!=b.stamp||a.tableHash!=b.tableHash)same=false;}
        if(same)for(const auto& f:old.files)reuse.emplace(Key(old,f),&f);
    }
    uint64_t needed=0,largest=0;
    for(size_t i=0;i<archives.size();++i)for(const auto& e:archives[i].entries){auto relative=fs::path(manifest.sources[i].group)/fs::path(e.name);loose::CheckOutputPath(output,relative);if(!fs::exists(loose::Extended(output/relative)))needed+=e.unpacked;largest=std::max<uint64_t>(largest,e.unpacked);}
    ULARGE_INTEGER available{};if(!GetDiskFreeSpaceExW(loose::Extended(output).c_str(),&available,nullptr,nullptr)||available.QuadPart<needed+largest)Fail("Not enough free space for the decoded files and a temporary file");
    manifest.files.reserve(size_t(progress.totalFiles));Result result;result.output=output;
    for(size_t i=0;i<archives.size();++i){
        std::ifstream input(archives[i].path,std::ios::binary);
        for(const auto& e:archives[i].entries){
            CheckCancel(cancel);const auto key=manifest.sources[i].group+"/"+e.name;const auto relative=fs::path(manifest.sources[i].group)/fs::path(e.name);const auto target=output/relative;
            loose::CheckOutputPath(output,relative);progress.current=relative.wstring();if(report)report(progress);
            loose::File record;record.source=uint32_t(i);record.name=e.name;record.size=e.unpacked;
            const bool exists=fs::exists(loose::Extended(target));const auto prior=reuse.find(key);
            if(exists&&prior!=reuse.end()&&prior->second->size==e.unpacked&&fs::file_size(loose::Extended(target))==e.unpacked&&loose::HashFile(target)==prior->second->hash){
                record.hash=prior->second->hash;progress.bytes+=e.unpacked;++result.reused;
            }else{
                fs::create_directories(loose::Extended(target.parent_path()));loose::CheckOutputPath(output,relative);
                Temporary temp(target);loose::Sha256 hash;uint64_t written=0;
                pac::Decode(input,e,[&](const unsigned char* data,size_t bytes){CheckCancel(cancel);DWORD done=0;if(!WriteFile(temp.file.h,data,DWORD(bytes),&done,nullptr)||done!=bytes)Fail("Output write failed");hash.Add(data,bytes);written+=bytes;progress.bytes+=bytes;if(report)report(progress);},cancel);
                if(written!=e.unpacked)Fail("Decoded output size mismatch");record.hash=hash.Finish();
                if(exists){
                    if(fs::file_size(loose::Extended(target))!=e.unpacked||loose::HashFile(target)!=record.hash)Fail("Existing output differs; choose a fresh output folder to preserve it");++result.reused;
                }else temp.Publish(target);
            }
            manifest.files.push_back(std::move(record));++progress.files;if(report)report(progress);
        }
    }
    CheckCancel(cancel);Temporary temp(manifestPath);temp.file.Close();loose::WriteManifest(temp.path,manifest);
    auto validated=loose::ReadManifest(temp.path);if(validated.files.size()!=manifest.files.size())Fail("Manifest roundtrip failed");
    temp.Publish(manifestPath,true);result.files=progress.files;result.bytes=progress.bytes;return result;
}

Result Run(const fs::path& selected,const fs::path& output,const Report& report,const pac::Cancel& cancel){
    const auto dlc=FindDlc(selected);
    auto result=RunFolder(selected,output,report,cancel);
    if(!dlc.empty()){
        CheckCancel(cancel);
        const auto extra=RunFolder(dlc,loose::DlcOutput(output),[&](const Progress& value){
            if(report){auto p=value;p.current=L"DLC: "+p.current;report(p);}},cancel);
        result.files+=extra.files;result.bytes+=extra.bytes;result.reused+=extra.reused;
    }
    return result;
}

static Result VerifyFolder(const fs::path& output,const Report& report,const pac::Cancel& cancel){
    loose::CheckOutputPath(output,L"manifest.vii");const auto manifest=loose::ReadManifest(output/L"manifest.vii");Progress p;p.totalFiles=manifest.files.size();for(const auto& f:manifest.files)p.totalBytes+=f.size;
    for(const auto& f:manifest.files){
        CheckCancel(cancel);const auto relative=fs::path(manifest.sources[f.source].group)/fs::path(f.name);loose::CheckOutputPath(output,relative);p.current=relative.wstring();if(report)report(p);
        if(fs::file_size(loose::Extended(output/relative))!=f.size||loose::HashFile(output/relative)!=f.hash)Fail("Extracted file verification failed");++p.files;p.bytes+=f.size;if(report)report(p);
    }
    return {p.files,p.bytes,0,output};
}
Result Verify(const fs::path& output,const Report& report,const pac::Cancel& cancel){
    auto result=VerifyFolder(output,report,cancel);
    const auto dlc=loose::DlcOutput(output);
    if(fs::exists(loose::Extended(dlc/L"manifest.vii"))){
        const auto extra=VerifyFolder(dlc,report,cancel);result.files+=extra.files;result.bytes+=extra.bytes;
    }
    return result;
}
}
