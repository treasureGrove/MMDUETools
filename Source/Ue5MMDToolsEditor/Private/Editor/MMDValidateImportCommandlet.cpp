#include "MMDValidateImportCommandlet.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

UMMDValidateImportCommandlet::UMMDValidateImportCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
	ShowErrorCount = false;
}

int32 UMMDValidateImportCommandlet::Main(const FString& Params)
{
	int32 PassedCount = 0;
	int32 FailedCount = 0;
	TArray<TSharedPtr<FJsonValue>> TestResults;
	TArray<TSharedPtr<FJsonValue>> ReportErrors;

#if WITH_DEV_AUTOMATION_TESTS
	FAutomationTestFramework& Framework = FAutomationTestFramework::Get();
	Framework.LoadTestModules();
	Framework.SetRequestedTestFilter(EAutomationTestFlags::EngineFilter);
	TArray<FAutomationTestInfo> TestInfo;
	Framework.GetValidTestNames(TestInfo);
	TestInfo.RemoveAll([](const FAutomationTestInfo& Info)
	{
		const FString Name = Info.GetFullTestPath();
		return !Name.StartsWith(TEXT("Ue5MMDTools.Import.")) && !Name.StartsWith(TEXT("Ue5MMDTools.Physics."));
	});
	TestInfo.Sort([](const FAutomationTestInfo& A, const FAutomationTestInfo& B)
	{
		return A.GetFullTestPath() < B.GetFullTestPath();
	});

	for (const FAutomationTestInfo& Info : TestInfo)
	{
		FAutomationTestExecutionInfo ExecutionInfo;
		TArray<TSharedPtr<FJsonValue>> Errors;
		bool bPassed = false;
		Framework.StartTestByName(Info.GetTestName(), 0, Info.GetFullTestPath());
		if (GIsAutomationTesting)
		{
			// 该入口只运行同步测试，遇到 latent 测试时明确失败，避免未执行完便误报通过。
			const bool bHasLatentCommands = !Framework.IsLatentCommandQueueEmpty();
			if (bHasLatentCommands)
			{
				Framework.DequeueAllCommands();
				Errors.Add(MakeShared<FJsonValueString>(TEXT("Latent tests are unsupported by this commandlet")));
			}
			bPassed = Framework.StopTest(ExecutionInfo) && !bHasLatentCommands;
		}
		else
		{
			Errors.Add(MakeShared<FJsonValueString>(TEXT("Automation test did not start")));
		}
		for (const FAutomationExecutionEntry& Entry : ExecutionInfo.GetEntries())
		{
			if (Entry.Event.Type == EAutomationEventType::Error)
			{
				Errors.Add(MakeShared<FJsonValueString>(Entry.Event.Message));
			}
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("test"), Info.GetFullTestPath());
		Result->SetStringField(TEXT("status"), bPassed ? TEXT("PASS") : TEXT("FAIL"));
		Result->SetBoolField(TEXT("passed"), bPassed);
		Result->SetNumberField(TEXT("duration_seconds"), ExecutionInfo.Duration);
		Result->SetArrayField(TEXT("errors"), Errors);
		TestResults.Add(MakeShared<FJsonValueObject>(Result));
		PassedCount += bPassed ? 1 : 0;
		FailedCount += bPassed ? 0 : 1;
		UE_LOG(LogTemp, Display, TEXT("[MMDValidateImport] %s %s (%.3fs)"),
			bPassed ? TEXT("PASS") : TEXT("FAIL"), *Info.GetFullTestPath(), ExecutionInfo.Duration);
	}
	if (TestInfo.IsEmpty())
	{
		ReportErrors.Add(MakeShared<FJsonValueString>(TEXT("No import or physics tests available for CommandletContext")));
		++FailedCount;
	}
#else
	ReportErrors.Add(MakeShared<FJsonValueString>(TEXT("WITH_DEV_AUTOMATION_TESTS is disabled")));
	++FailedCount;
#endif

	TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	Report->SetNumberField(TEXT("test_count"), TestResults.Num());
	Report->SetNumberField(TEXT("passed"), PassedCount);
	Report->SetNumberField(TEXT("failed"), FailedCount);
	Report->SetBoolField(TEXT("success"), FailedCount == 0);
	Report->SetArrayField(TEXT("tests"), TestResults);
	Report->SetArrayField(TEXT("errors"), ReportErrors);
	FString Json;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BackgroundValidation"));
	const FString OutputPath = FPaths::Combine(Directory, TEXT("import-tests.json"));
	if (!FJsonSerializer::Serialize(Report, Writer) || !IFileManager::Get().MakeDirectory(*Directory, true) ||
		!FFileHelper::SaveStringToFile(Json, *OutputPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogTemp, Error, TEXT("[MMDValidateImport] 无法写入报告：%s"), *OutputPath);
		return 1;
	}
	UE_LOG(LogTemp, Display, TEXT("[MMDValidateImport] Done: %d tests, %d passed, %d failed. Report: %s"),
		TestResults.Num(), PassedCount, FailedCount, *OutputPath);
	return FailedCount == 0 ? 0 : 1;
}
