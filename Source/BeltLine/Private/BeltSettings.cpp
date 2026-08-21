// Copyright 2026 Simulated Flow. All Rights Reserved.

#include "BeltSettings.h"

UBeltSettings::UBeltSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("BeltLine");
}

FName UBeltSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

FName UBeltSettings::GetSectionName() const
{
	return TEXT("BeltLine");
}

const UBeltSettings& UBeltSettings::Get()
{
	const UBeltSettings* Settings = GetDefault<UBeltSettings>();
	check(Settings);
	return *Settings;
}
