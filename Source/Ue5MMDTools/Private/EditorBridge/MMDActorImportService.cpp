#include "EditorBridge/MMDActorImportService.h"
namespace { IMMDActorImportService* GActorImportService = nullptr; }
IMMDActorImportService* IMMDActorImportService::Get() { return GActorImportService; }
void IMMDActorImportService::Set(IMMDActorImportService* Service) { check(IsInGameThread()); GActorImportService = Service; }
