// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Components/PoseableMeshComponent.h"

#include "IKModuleCharacter.generated.h"

UCLASS(config=Game)
class AIKModuleCharacter : public ACharacter
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	class USpringArmComponent* CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	class UCameraComponent* FollowCamera;

public:
	AIKModuleCharacter();
	void Tick(float DeltaTime) override;
	void SolveCCD(FName TipBoneName, FName RootBoneName, FVector TargetLocation, float Precision, int32 MaxIterations);
	void SolveFABRIK(FName TipBoneName, FName RootBoneName, FVector TargetLocation, float Precision, int32 MaxIterations);
	void SolveJacobianTranspose(FName TipBoneName, FName RootBoneName, FVector TargetLocation, float Precision);
	void SolveJacobianPinv(FName TipBoneName, FName RootBoneName, FVector TargetLocation, float Precision);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Camera)
	float BaseTurnRate;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Camera)
	float BaseLookUpRate;

	UPROPERTY(VisibleAnywhere, Category = "IK")
	UPoseableMeshComponent* poseableMeshComp;

protected:
	void MoveForward(float Value);
	void MoveRight(float Value);
	void TurnAtRate(float Rate);
	void LookUpAtRate(float Rate);

protected:
	// APawn interface
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	// End of APawn interface

public:
	FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CameraBoom; }
	FORCEINLINE class UCameraComponent* GetFollowCamera() const { return FollowCamera; }
};

