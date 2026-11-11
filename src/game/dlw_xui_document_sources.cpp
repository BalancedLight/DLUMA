#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <windows.h>
#include <objidl.h>
#include <xmllite.h>
#include <cstring>
#include <cwchar>
#include "dlw_xui_document_sources.h"
extern "C" void dluma_sha256_bytes_hex(const void*,size_t,char*);
namespace {
// Call sites validate the destination capacity before this copied write.
static void CopyText(char* out,const char* value)
{std::memcpy(out,value,std::strlen(value)+1);}
constexpr unsigned kNodes=8192,kDepth=256,kXmlNodes=262144,kBuckets=16384;
struct Node {char id[512],type[128];unsigned parent;uint64_t hash;};
struct Graph {Node nodes[kNodes];unsigned buckets[kBuckets],count;char resources[2048][640];unsigned resource_count;};
struct Retained {HANDLE file;unsigned char* bytes;DWORD size;Graph* graph;};
struct Frame {char name[128];unsigned object;};
struct XmlApi {HMODULE xml,shlwapi;HRESULT(WINAPI* create)(REFIID,void**,IMalloc*);IStream*(WINAPI* stream)(const BYTE*,UINT);};
static bool Api(XmlApi* a)
{
 a->xml=LoadLibraryExW(L"xmllite.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
 a->shlwapi=LoadLibraryExW(L"shlwapi.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
 if(!a->xml||!a->shlwapi)return false;
 a->create=reinterpret_cast<decltype(a->create)>(GetProcAddress(a->xml,"CreateXmlReader"));
 a->stream=reinterpret_cast<decltype(a->stream)>(GetProcAddress(a->shlwapi,"SHCreateMemStream"));
 return a->create&&a->stream;
}
static bool Text(const WCHAR* p,UINT n,char* out,unsigned cap)
{
 if(!n){out[0]=0;return true;}
 int written=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,p,static_cast<int>(n),out,static_cast<int>(cap-1),nullptr,nullptr);
 if(written<=0)return false;out[written]=0;return true;
}
static uint64_t Hash(uint64_t h,const char* s)
{for(unsigned i=0;s[i];++i){h^=static_cast<unsigned char>(s[i]);h*=1099511628211ULL;}return h;}
static bool ResourceTag(const char* name)
{static const char* const tags[]={"ImagePath","Font","Material","MaskTexture","SoundName","FileName","Filename","ScriptName","Include","SourceFile"};for(const auto* tag:tags)if(!std::strcmp(name,tag))return true;return false;}
static bool Resource(Graph& graph,const char* name,const char* value)
{
 if(!*value)return true;size_t n=std::strlen(name),v=std::strlen(value);if(n+v+2>640)return false;char key[640];std::memcpy(key,name,n);key[n]=':';std::memcpy(key+n+1,value,v+1);
 for(unsigned i=0;i<graph.resource_count;++i)if(!std::strcmp(key,graph.resources[i]))return true;
 if(graph.resource_count==2048)return false;CopyText(graph.resources[graph.resource_count++],key);return true;
}
static bool SamePath(const Graph& a,unsigned ai,const Graph& b,unsigned bi)
{
 for(unsigned depth=0;depth<kDepth;++depth){const auto& x=a.nodes[ai];const auto& y=b.nodes[bi];if(std::strcmp(x.id,y.id))return false;
  if(x.parent==UINT32_MAX||y.parent==UINT32_MAX)return x.parent==y.parent;ai=x.parent;bi=y.parent;}
 return false;
}
static unsigned Find(const Graph& g,const Graph& key,unsigned ki)
{
 unsigned bucket=static_cast<unsigned>(key.nodes[ki].hash)&(kBuckets-1);
 for(unsigned i=0;i<64;++i){unsigned entry=g.buckets[bucket];if(!entry)return UINT32_MAX;--entry;
  if(g.nodes[entry].hash==key.nodes[ki].hash&&SamePath(g,entry,key,ki))return entry;bucket=(bucket+1)&(kBuckets-1);}
 return UINT32_MAX;
}
static bool Insert(Graph& g,unsigned i)
{
 unsigned bucket=static_cast<unsigned>(g.nodes[i].hash)&(kBuckets-1);
 for(unsigned probe=0;probe<64;++probe){if(!g.buckets[bucket]){g.buckets[bucket]=i+1;return true;}
  unsigned other=g.buckets[bucket]-1;if(g.nodes[other].hash==g.nodes[i].hash&&SamePath(g,other,g,i))return false;bucket=(bucket+1)&(kBuckets-1);}
 return false;
}
static bool Generic(const char* s)
{
 static const char* const names[]={"IUIElement","IUIElementTimeLine","IUIElementView","IUIElementControl","IUIVisual","IUIGroup","IUIImage","IUIImageAdvancedTexture","IUIText","IUITextPresenter","IUIImagePresenter","IUIShape","IUIAARectangle","IUIProgressText","IUIEffectGroup","IUICanvas","IUISoundEmitter","UIWrapPanel","UIStackPanel","UIButton","UIMaskedImage","UIMaskedText","UITextWithGameActions"};
 for(const auto* n:names)if(!std::strcmp(s,n))return true;return false;
}
static const char* Type(const char* tag)
{
 struct Pair {const char* tag;const char* type;};
 static const Pair pairs[]={{"AdvGroup","IUIGroup"},{"XuiGroup","IUIGroup"},{"MyImage","IUIImage"},{"XuiImage","IUIImage"},{"MyText","IUIText"},{"XuiText","IUIText"},{"XuiScene","IUIScene"},{"XuiElement","IUIElement"},{"XuiVisual","IUIVisual"},{"XuiControl","IUIElementControl"},{"AdvControl","IUIElementControl"},{"XuiButton","UIButton"},{"AdvButton","UIButton"},{"XuiTextPresenter","IUITextPresenter"},{"MyTextPresenter","IUITextPresenter"}};
 for(const auto& p:pairs)if(!std::strcmp(tag,p.tag))return p.type;return tag;
}
static bool Parse(Retained* source)
{
 XmlApi api={};IXmlReader* reader=nullptr;IStream* stream=nullptr;bool ok=false;
 if(!Api(&api))goto finish;
 if(FAILED(api.create(__uuidof(IXmlReader),reinterpret_cast<void**>(&reader),nullptr)))goto finish;
 if(FAILED(reader->SetProperty(XmlReaderProperty_DtdProcessing,DtdProcessing_Prohibit)))goto finish;
 stream=api.stream(source->bytes,source->size);if(!stream||FAILED(reader->SetInput(stream)))goto finish;
 {
  Frame frames[kDepth]={};unsigned properties=UINT32_MAX,property_object=UINT32_MAX,total=0,roots=0;char value_name[128]={},property_value[1024]={};
  XmlNodeType kind=XmlNodeType_None;HRESULT result;
  while((result=reader->Read(&kind))==S_OK){
   UINT depth=0;const WCHAR* text=nullptr;UINT length=0;
   if((kind==XmlNodeType_Element&&++total>kXmlNodes)||FAILED(reader->GetDepth(&depth))||depth>=kDepth)goto finish;
   /* XmlLite reports an end tag after popping its element context. Its
    * reported depth is one greater than that element's start tag. */
   if(kind==XmlNodeType_EndElement){if(!depth)goto finish;--depth;}
   if(kind==XmlNodeType_Element){
    if(FAILED(reader->GetNamespaceUri(&text,&length))||length||FAILED(reader->GetLocalName(&text,&length))||!Text(text,length,frames[depth].name,sizeof(frames[depth].name)))goto finish;
    frames[depth].object=UINT32_MAX;
    if(depth==0&&std::strcmp(frames[0].name,"XuiCanvas"))goto finish;
    if(!std::strcmp(frames[depth].name,"Properties")){
     if(!depth||properties!=UINT32_MAX||source->graph->count>=kNodes)goto finish;
     unsigned index=source->graph->count++;Node& node=source->graph->nodes[index];node.parent=UINT32_MAX;
     for(unsigned ancestor=depth-1;ancestor>0;--ancestor)if(frames[ancestor-1].object!=UINT32_MAX){node.parent=frames[ancestor-1].object;break;}
     CopyText(node.type,Type(frames[depth-1].name));frames[depth-1].object=index;properties=depth;property_object=index;
    }else if(properties!=UINT32_MAX&&depth==properties+1){CopyText(value_name,frames[depth].name);property_value[0]=0;}
    if(reader->IsEmptyElement()){
     if(depth==properties)goto finish;
     value_name[0]=0;
    }
   }else if(kind==XmlNodeType_Text||kind==XmlNodeType_CDATA||kind==XmlNodeType_Whitespace){
    if(properties!=UINT32_MAX&&depth==properties+2&&(!std::strcmp(value_name,"Id")||!std::strcmp(value_name,"ClassOverride")||ResourceTag(value_name))){
     if(FAILED(reader->GetValue(&text,&length)))goto finish;char part[1024];
     if(!Text(text,length,part,sizeof(part))||std::strlen(property_value)+std::strlen(part)>=sizeof(property_value))goto finish;
     std::memcpy(property_value+std::strlen(property_value),part,std::strlen(part)+1);
    }
   }else if(kind==XmlNodeType_EndElement){
    if(depth==properties){
     Node& node=source->graph->nodes[property_object];
     if(!node.id[0]&&std::strcmp(frames[depth-1].name,"XuiCanvas"))goto finish;
     node.hash=Hash(node.parent==UINT32_MAX?1469598103934665603ULL:source->graph->nodes[node.parent].hash,"/");node.hash=Hash(node.hash,node.id);
     if(!Insert(*source->graph,property_object))goto finish;
     if(!std::strcmp(node.type,"HUDScreenBase")){if(std::strcmp(node.id,"HUD_DI"))goto finish;++roots;}
     properties=property_object=UINT32_MAX;value_name[0]=0;
    }else if(properties!=UINT32_MAX&&depth==properties+1){
     Node& node=source->graph->nodes[property_object];
     if(!std::strcmp(value_name,"Id")){if(std::strlen(property_value)>=sizeof(node.id))goto finish;CopyText(node.id,property_value);}
     else if(!std::strcmp(value_name,"ClassOverride")&&property_value[0]){if(std::strlen(property_value)>=sizeof(node.type))goto finish;CopyText(node.type,property_value);}
     else if(ResourceTag(value_name)&&!Resource(*source->graph,value_name,property_value))goto finish;
     value_name[0]=property_value[0]=0;
    }
   }else if(kind==XmlNodeType_DocumentType)goto finish;
  }
  ok=result==S_FALSE&&properties==UINT32_MAX&&roots==1&&source->graph->count>0;
 }
finish:
 if(reader)reader->Release();if(stream)stream->Release();if(api.xml)FreeLibrary(api.xml);if(api.shlwapi)FreeLibrary(api.shlwapi);return ok;
}
static void Dispose(Retained* p)
{if(!p)return;if(p->file!=INVALID_HANDLE_VALUE&&p->file)CloseHandle(p->file);if(p->bytes)HeapFree(GetProcessHeap(),0,p->bytes);if(p->graph)HeapFree(GetProcessHeap(),0,p->graph);HeapFree(GetProcessHeap(),0,p);}
}
extern "C" int dluma_xui_document_source_acquire(void*,const char* path,DlumaXuiDocumentSource* out)
{
 if(!out)return 0;std::memset(out,0,sizeof(*out));if(!path||std::strlen(path)>=DLUMA_XUI_DOCUMENT_PATH_CAPACITY)return 0;
 WCHAR wide[DLUMA_XUI_DOCUMENT_PATH_CAPACITY],final_path[DLUMA_XUI_DOCUMENT_PATH_CAPACITY];
 if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,wide,DLUMA_XUI_DOCUMENT_PATH_CAPACITY))return 0;
 auto* p=static_cast<Retained*>(HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(Retained)));if(!p)return 0;p->file=INVALID_HANDLE_VALUE;
 p->file=CreateFileW(wide,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
 LARGE_INTEGER size={};DWORD read=0;char digest[65]={};
 if(p->file==INVALID_HANDLE_VALUE||!GetFileSizeEx(p->file,&size)||size.QuadPart<=0||size.QuadPart>DLUMA_XUI_DOCUMENT_MAX_BYTES)goto fail;
 p->size=static_cast<DWORD>(size.QuadPart);p->bytes=static_cast<unsigned char*>(HeapAlloc(GetProcessHeap(),0,p->size));p->graph=static_cast<Graph*>(HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(Graph)));
 if(!p->bytes||!p->graph||!ReadFile(p->file,p->bytes,p->size,&read,nullptr)||read!=p->size||!Parse(p))goto fail;
 {DWORD length=GetFinalPathNameByHandleW(p->file,final_path,DLUMA_XUI_DOCUMENT_PATH_CAPACITY,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);if(!length||length>=DLUMA_XUI_DOCUMENT_PATH_CAPACITY)goto fail;
  const WCHAR* native=final_path;if(!std::wcsncmp(native,L"\\\\?\\",4))native+=4;
  if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,native,-1,out->path,DLUMA_XUI_DOCUMENT_PATH_CAPACITY,nullptr,nullptr)||std::strlen(out->path)>=512)goto fail;}
 dluma_sha256_bytes_hex(p->bytes,p->size,digest);
 for(unsigned i=0;i<32;++i){unsigned byte=0;for(unsigned j=0;j<2;++j){char c=digest[i*2+j];unsigned v=c>='0'&&c<='9'?static_cast<unsigned>(c-'0'):c>='a'&&c<='f'?static_cast<unsigned>(c-'a'+10):c>='A'&&c<='F'?static_cast<unsigned>(c-'A'+10):99;if(v>15)goto fail;byte=(byte<<4)|v;}out->sha256[i]=static_cast<unsigned char>(byte);}
 out->handle=reinterpret_cast<uintptr_t>(p);out->bytes=p->size;return 1;
fail:Dispose(p);std::memset(out,0,sizeof(*out));return 0;
}
extern "C" int dluma_xui_document_source_compatible(void*,const DlumaXuiDocumentSource* a,const DlumaXuiDocumentSource* b)
{
 if(!a||!b||!a->handle||!b->handle)return 0;const Graph& old=*reinterpret_cast<Retained*>(a->handle)->graph;const Graph& next=*reinterpret_cast<Retained*>(b->handle)->graph;
 for(unsigned i=0;i<old.count;++i){unsigned match=Find(next,old,i);if(match==UINT32_MAX||std::strcmp(old.nodes[i].type,next.nodes[match].type))return 0;}
 for(unsigned i=0;i<next.count;++i)if(Find(old,next,i)==UINT32_MAX&&!Generic(next.nodes[i].type))return 0;
 // This first native route permits compatible HUDs using their existing
 // assets. New dependencies require their own qualified retained-resource
 // owner; a missing font/texture must fail before the old HUD is removed.
 for(unsigned i=0;i<next.resource_count;++i){bool found=false;for(unsigned j=0;j<old.resource_count;++j)if(!std::strcmp(next.resources[i],old.resources[j])){found=true;break;}if(!found)return 0;}
 return 1;
}
extern "C" int dluma_xui_document_source_release(void*,DlumaXuiDocumentSource* s)
{
 if(!s)return 0;if(!s->handle)return 1;auto* p=reinterpret_cast<Retained*>(s->handle);
 if(p->file!=INVALID_HANDLE_VALUE&&!CloseHandle(p->file))return 0;p->file=INVALID_HANDLE_VALUE;Dispose(p);std::memset(s,0,sizeof(*s));return 1;
}
