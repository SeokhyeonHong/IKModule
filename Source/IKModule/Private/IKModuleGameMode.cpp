// Copyright Epic Games, Inc. All Rights Reserved.

#include "IKModuleGameMode.h"
#include "IKModuleCharacter.h"
#include "UObject/ConstructorHelpers.h"

AIKModuleGameMode::AIKModuleGameMode()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnBPClass(TEXT("/Game/ThirdPersonCPP/Blueprints/ThirdPersonCharacter"));
	if (PlayerPawnBPClass.Class != NULL)
	{
		DefaultPawnClass = PlayerPawnBPClass.Class;
	}
}
