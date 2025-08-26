// Fill out your copyright notice in the Description page of Project Settings.


#include "MMDFactory.h"

UMMDFactory::UMMDFactory()
{
	Formats.Add(TEXT("pmx;MikuMikuDance Model"));
}

UObject* UMMDFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
	return nullptr;
}
