// Included after the native resource scope and file identity helpers.
bool ReadArchiveInputMemory(void*,std::uintptr_t address,void* out,std::size_t bytes)noexcept
{ return ReadBytes(address,out,bytes); }
bool AuthorizeArchiveInput(void*,const files::FileRecord& file)noexcept
{ return g_filesystem_full_profile_valid && filebridge::FreshZipReturnMatches(&g_native_file_bridge,file); }
archiveinput::Access ProductionArchiveInputAccess()noexcept
{
    return {g_context.filesystem_base,&ReadArchiveInputMemory,&AuthorizeArchiveInput,
        nullptr,nullptr,nullptr,g_filesystem_full_profile_valid};
}
void ObserveFreshArchiveInput(void*,const files::FileRecord& file,std::uint32_t slot)noexcept
{
    if(!g_archive_input_observer_ready ||
       (!g_xui_content_load_depth && !resourcegate::HasActiveInputScope(&g_native_resources.gateway)))return;
    archiveinput::Digest digest={};digest.file=file;digest.result=archiveinput::Result::InvalidArgument;
    void* scratch=HeapAlloc(GetProcessHeap(),0,archiveinput::kChunkBytes);
    if(scratch) {
        (void)archiveinput::CaptureFresh(ProductionArchiveInputAccess(),file,64ULL*1024ULL*1024ULL,
            scratch,archiveinput::kChunkBytes,&digest);
        HeapFree(GetProcessHeap(),0,scratch);
    }
    if(!archiveinput::Publish(&g_archive_input_cache,slot,digest) || !digest.complete)
        dluma_atomic_increment(&g_archive_input_failures);
    if(digest.result==archiveinput::Result::RestoreFailed)
        files::RecordCoverageLoss(&g_native_file_bridge.ledger,files::Failure::InvalidToken);
}
bool ConfigureProductionArchiveInputs()noexcept
{
    return archiveinput::ValidateNativeAccess(ProductionArchiveInputAccess()) &&
        filebridge::ConfigureFreshZipObserver(&g_native_file_bridge,&ObserveFreshArchiveInput,nullptr);
}
