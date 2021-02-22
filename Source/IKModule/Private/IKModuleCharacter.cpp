// Copyright Epic Games, Inc. All Rights Reserved.

#include "IKModuleCharacter.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"

#include <Eigen/Dense>

AIKModuleCharacter::AIKModuleCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	// set our turn rates for input
	BaseTurnRate = 45.f;
	BaseLookUpRate = 45.f;

	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true; // Character moves in the direction of input...	
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 540.0f, 0.0f); // ...at this rotation rate
	GetCharacterMovement()->JumpZVelocity = 600.f;
	GetCharacterMovement()->AirControl = 0.2f;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 300.0f; // The camera follows at this distance behind the character	
	CameraBoom->bUsePawnControlRotation = true; // Rotate the arm based on the controller

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName); // Attach the camera to the end of the boom and let the boom adjust to match the controller orientation
	FollowCamera->bUsePawnControlRotation = false; // Camera does not rotate relative to arm


	poseableMeshComp = CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("IK"));
	poseableMeshComp->SetupAttachment(RootComponent);
}

void AIKModuleCharacter::SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent)
{
	// Set up gameplay key bindings
	check(PlayerInputComponent);
	PlayerInputComponent->BindAction("Jump", IE_Pressed, this, &ACharacter::Jump);
	PlayerInputComponent->BindAction("Jump", IE_Released, this, &ACharacter::StopJumping);

	PlayerInputComponent->BindAxis("MoveForward", this, &AIKModuleCharacter::MoveForward);
	PlayerInputComponent->BindAxis("MoveRight", this, &AIKModuleCharacter::MoveRight);

	// We have 2 versions of the rotation bindings to handle different kinds of devices differently
	// "turn" handles devices that provide an absolute delta, such as a mouse.
	// "turnrate" is for devices that we choose to treat as a rate of change, such as an analog joystick
	PlayerInputComponent->BindAxis("Turn", this, &APawn::AddControllerYawInput);
	PlayerInputComponent->BindAxis("TurnRate", this, &AIKModuleCharacter::TurnAtRate);
	PlayerInputComponent->BindAxis("LookUp", this, &APawn::AddControllerPitchInput);
	PlayerInputComponent->BindAxis("LookUpRate", this, &AIKModuleCharacter::LookUpAtRate);
}

void AIKModuleCharacter::TurnAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerYawInput(Rate * BaseTurnRate * GetWorld()->GetDeltaSeconds());
}

void AIKModuleCharacter::LookUpAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerPitchInput(Rate * BaseLookUpRate * GetWorld()->GetDeltaSeconds());
}

void AIKModuleCharacter::MoveForward(float Value)
{
	if ((Controller != nullptr) && (Value != 0.0f))
	{
		// find out which way is forward
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		AddMovementInput(Direction, Value);
	}
}

void AIKModuleCharacter::MoveRight(float Value)
{
	if ( (Controller != nullptr) && (Value != 0.0f) )
	{
		// find out which way is right
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);
	
		// get right vector 
		const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
		// add movement in that direction
		AddMovementInput(Direction, Value);
	}
}

void AIKModuleCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
	FName TipBoneName = FName("hand_l");
	FName RootBoneName = FName("upperarm_l");
	//SolveFABRIK(TipBoneName, RootBoneName, FVector(0, 0, 260), 1.0f, 10);
	SolveJacobianTranspose(TipBoneName, RootBoneName, FVector(0, 0, 260), 1.0f);

}

void AIKModuleCharacter::SolveCCD(FName TipBoneName, FName RootBoneName, FVector TargetLocation, float Precision, int32 MaxIterations)
{
	// bone hierarchy에서 tip bone 위에 root bone이 있는지 확인
	FName BoneName = TipBoneName;
	TArray<FName> BoneNames;
	BoneNames.Add(BoneName);
	while (!BoneName.IsEqual(RootBoneName))
	{
		BoneName = poseableMeshComp->GetParentBone(BoneName);
		BoneNames.Insert(BoneName, 0);
	}
	if (BoneName.IsEqual("None"))
	{
		UE_LOG(LogTemp, Warning, TEXT("TipBoneName is NOT a child of RootBoneName"));
		return;
	}
	
	// solve
	int32 IterationCount = 0;
	float Distance = FVector::Dist(poseableMeshComp->GetBoneLocation(TipBoneName), TargetLocation);
	
	while (Distance > Precision && IterationCount++ < MaxIterations)
	{
		for (int32 Index = BoneNames.Num() - 2; Index >= 0; --Index)
		{
			FVector TipLocation = poseableMeshComp->GetBoneLocationByName(TipBoneName, EBoneSpaces::WorldSpace);

			FName CurrentBoneName = BoneNames[Index];
			FVector CurrentBoneLocation = poseableMeshComp->GetBoneLocationByName(CurrentBoneName, EBoneSpaces::WorldSpace);

			FVector ToEnd = TipLocation - CurrentBoneLocation;
			FVector ToTarget = TargetLocation - CurrentBoneLocation;
			ToEnd.Normalize();
			ToTarget.Normalize();

			float Angle = FMath::Acos(FVector::DotProduct(ToEnd, ToTarget));
			FVector RotationAxis = FVector::CrossProduct(ToEnd, ToTarget);
			if (RotationAxis.SizeSquared() > 0.f)
			{
				FQuat DeltaRotation(RotationAxis, Angle);
				FQuat CurrentQuat = poseableMeshComp->GetBoneQuaternion(CurrentBoneName);
				FQuat NewQuat = DeltaRotation * CurrentQuat;
				NewQuat.Normalize();
				poseableMeshComp->SetBoneRotationByName(CurrentBoneName, NewQuat.Rotator(), EBoneSpaces::WorldSpace);
			}
		}
		Distance = FVector::Dist(poseableMeshComp->GetBoneLocation(TipBoneName), TargetLocation);
	}
}

void AIKModuleCharacter::SolveFABRIK(FName TipBoneName, FName RootBoneName, FVector TargetLocation, float Precision, int32 MaxIterations)
{
	// bone hierarchy에서 tip bone 위에 root bone이 있는지 확인
	FName BoneName = TipBoneName;
	TArray<FName> BoneNames;
	TArray<FVector> OriginalLocation;
	BoneNames.Add(BoneName);
	OriginalLocation.Add(poseableMeshComp->GetBoneLocationByName(BoneName, EBoneSpaces::WorldSpace));
	while (!BoneName.IsEqual(RootBoneName))
	{
		BoneName = poseableMeshComp->GetParentBone(BoneName);
		BoneNames.Insert(BoneName, 0);
		OriginalLocation.Insert(poseableMeshComp->GetBoneLocationByName(BoneName, EBoneSpaces::WorldSpace), 0);
	}
	if (BoneName.IsEqual("None"))
	{
		UE_LOG(LogTemp, Warning, TEXT("TipBoneName is NOT a child of RootBoneName"));
		return;
	}
	
	// bone length
	TArray<float> BoneLengths;
	float BoneLengthSum = 0.f;
	for (int32 Index = 0; Index < BoneNames.Num() - 1; ++Index)
	{
		FVector CurrentLocation = poseableMeshComp->GetBoneLocationByName(BoneNames[Index], EBoneSpaces::WorldSpace);
		FVector NextLocation = poseableMeshComp->GetBoneLocationByName(BoneNames[Index + 1], EBoneSpaces::WorldSpace);
		
		float BoneLength = FVector::Dist(CurrentLocation, NextLocation);
		BoneLengths.Add(BoneLength);
		BoneLengthSum += BoneLength;
	}

	// reachability calculation
	FVector RootLocation = poseableMeshComp->GetBoneLocationByName(RootBoneName, EBoneSpaces::WorldSpace);
	float RootToTargetDist = FVector::Dist(RootLocation, TargetLocation);

	// unreachable
	if (RootToTargetDist > BoneLengthSum)
	{
		for (int32 Index = 0; Index < BoneNames.Num() - 1; ++Index)
		{
			FVector BoneLocation = poseableMeshComp->GetBoneLocationByName(BoneNames[Index], EBoneSpaces::WorldSpace);
			float DistanceToTarget = FVector::Dist(BoneLocation, TargetLocation);
			float Lambda = BoneLengths[Index] / DistanceToTarget;

			FVector NewLocation = (1 - Lambda) * BoneLocation + Lambda * TargetLocation;
			poseableMeshComp->SetBoneLocationByName(BoneNames[Index + 1], NewLocation, EBoneSpaces::WorldSpace);
		}
	}

	// reachable
	else
	{
		int32 IterationCount = 0;
		float Distance = FVector::Dist(poseableMeshComp->GetBoneLocation(TipBoneName), TargetLocation);
		
		while (Distance > Precision && IterationCount++ < MaxIterations)
		{
			// forward reaching
			poseableMeshComp->SetBoneLocationByName(TipBoneName, TargetLocation, EBoneSpaces::WorldSpace);
			for (int32 Index = BoneNames.Num() - 2; Index >= 0; --Index)
			{
				FVector BoneLocation = poseableMeshComp->GetBoneLocationByName(BoneNames[Index], EBoneSpaces::WorldSpace);
				FVector NextBoneLocation = poseableMeshComp->GetBoneLocationByName(BoneNames[Index + 1], EBoneSpaces::WorldSpace);
				float JointDistance = FVector::Dist(BoneLocation, NextBoneLocation);
				float Lambda = BoneLengths[Index] / JointDistance;

				FVector NewLocation = (1 - Lambda) * NextBoneLocation + Lambda * BoneLocation;
				poseableMeshComp->SetBoneLocationByName(BoneNames[Index], NewLocation, EBoneSpaces::WorldSpace);
			}

			// backward reaching
			poseableMeshComp->SetBoneLocationByName(RootBoneName, RootLocation, EBoneSpaces::WorldSpace);
			for (int32 Index = 0; Index < BoneNames.Num() - 1; ++Index)
			{
				FVector BoneLocation = poseableMeshComp->GetBoneLocationByName(BoneNames[Index], EBoneSpaces::WorldSpace);
				FVector NextBoneLocation = poseableMeshComp->GetBoneLocationByName(BoneNames[Index + 1], EBoneSpaces::WorldSpace);
				float JointDistance = FVector::Dist(BoneLocation, NextBoneLocation);
				float Lambda = BoneLengths[Index] / JointDistance;

				FVector NewLocation = (1 - Lambda) * BoneLocation + Lambda * NextBoneLocation;
				poseableMeshComp->SetBoneLocationByName(BoneNames[Index + 1], NewLocation, EBoneSpaces::WorldSpace);
			}

			Distance = FVector::Dist(poseableMeshComp->GetBoneLocation(TipBoneName), TargetLocation);
		}
		
	}
	// orientation adjustment
	for (int32 Index = 0; Index < BoneNames.Num() - 1; ++Index)
	{
		FVector OriginalOrientation = OriginalLocation[Index + 1] - OriginalLocation[Index];
		FVector NewOrientation = poseableMeshComp->GetBoneLocationByName(BoneNames[Index + 1], EBoneSpaces::WorldSpace) - poseableMeshComp->GetBoneLocationByName(BoneNames[Index], EBoneSpaces::WorldSpace);
		OriginalOrientation.Normalize();
		NewOrientation.Normalize();

		float Angle = FMath::Acos(FVector::DotProduct(OriginalOrientation, NewOrientation));
		FVector RotationAxis = FVector::CrossProduct(OriginalOrientation, NewOrientation);
		if (RotationAxis.SizeSquared() > 0.f)
		{
			FQuat DeltaRotation(RotationAxis, Angle);
			FQuat BoneQuat = poseableMeshComp->GetBoneQuaternion(BoneNames[Index]);
			FQuat NewQuat = DeltaRotation * BoneQuat;
			NewQuat.Normalize();
			poseableMeshComp->SetBoneRotationByName(BoneNames[Index], NewQuat.Rotator(), EBoneSpaces::WorldSpace);
		}
	}
}

void AIKModuleCharacter::SolveJacobianTranspose(FName TipBoneName, FName RootBoneName, FVector TargetLocation, float Precision)
{
	// bone hierarchy에서 tip bone 위에 root bone이 있는지 확인
	FName BoneName = TipBoneName;
	TArray<FName> BoneNames;
	BoneNames.Add(BoneName);
	while (!BoneName.IsEqual(RootBoneName))
	{
		BoneName = poseableMeshComp->GetParentBone(BoneName);
		BoneNames.Insert(BoneName, 0);
	}
	if (BoneName.IsEqual("None"))
	{
		UE_LOG(LogTemp, Warning, TEXT("TipBoneName is NOT a child of RootBoneName"));
		return;
	}

	FVector TipBoneLocation = poseableMeshComp->GetBoneLocationByName(TipBoneName, EBoneSpaces::WorldSpace);
	float Distance = FVector::Dist(TipBoneLocation, TargetLocation);
	if (Distance > Precision)
	{
		// create Jacobian matrix
		Eigen::MatrixXf JacobianMat;
		int NumOfLinks = BoneNames.Num() - 1;
		JacobianMat.resize(3, NumOfLinks * 3);

		FVector TipLocation = poseableMeshComp->GetBoneLocationByName(TipBoneName, EBoneSpaces::WorldSpace);
		for (int32 Index = 0; Index < NumOfLinks; ++Index)
		{
			FVector BoneLocation = poseableMeshComp->GetBoneLocationByName(BoneNames[Index], EBoneSpaces::WorldSpace);

			FVector RotAxisX = poseableMeshComp->GetBoneAxis(BoneNames[Index], EAxis::X) - BoneLocation;
			FVector RotAxisY = poseableMeshComp->GetBoneAxis(BoneNames[Index], EAxis::Y) - BoneLocation;
			FVector RotAxisZ = poseableMeshComp->GetBoneAxis(BoneNames[Index], EAxis::Z) - BoneLocation;

			int ColumnOffset = Index * 3;

			if (RotAxisX.SizeSquared() > 0.f)
			{
				RotAxisX.Normalize();
				FVector DeltaX = FVector::CrossProduct(RotAxisX, TipLocation - BoneLocation);
				if (DeltaX.SizeSquared() > 0.f)
				{
					for (int Row = 0; Row < 3; ++Row)
						JacobianMat(Row, ColumnOffset) = DeltaX[Row];
				}
			}

			if (RotAxisY.SizeSquared() > 0.f)
			{
				RotAxisY.Normalize();
				FVector DeltaY = FVector::CrossProduct(RotAxisY, TipLocation - BoneLocation);
				if (DeltaY.SizeSquared() > 0.f)
				{
					DeltaY.Normalize();
					for (int Row = 0; Row < 3; ++Row)
						JacobianMat(Row, ColumnOffset + 1) = DeltaY[Row];
				}
			}

			if (RotAxisZ.SizeSquared() > 0.f)
			{
				RotAxisZ.Normalize();
				FVector DeltaZ = FVector::CrossProduct(RotAxisZ, TipLocation - BoneLocation);
				if (DeltaZ.SizeSquared() > 0.f)
				{
					for (int Row = 0; Row < 3; ++Row)
						JacobianMat(Row, ColumnOffset + 2) = DeltaZ[Row];
				}
			}
		}

		// calculate Jacobian Square
		Eigen::MatrixXf JacobianTranspose = JacobianMat.transpose();
		Eigen::MatrixXf JacobianSquare = JacobianMat * JacobianTranspose;
		
		// calculate effector derivative
		Eigen::Vector3f TargetEigen, TipEigen;
		for (int i = 0; i < 3; ++i)
		{
			TargetEigen(i) = TargetLocation[i];
			TipEigen(i) = TipLocation[i];
		}
		Eigen::Vector3f EffectorDerivatives = TargetEigen - TipEigen;

		// solve
		Eigen::Vector3f Result = JacobianSquare * EffectorDerivatives;
		Eigen::Vector3f EffectorDerivativesVector = EffectorDerivatives;
		float AlphaBottom = Result.dot(Result);
		float AlphaUp = EffectorDerivativesVector.dot(Result);

		Eigen::MatrixXf DeltaRotation;
		if (!FMath::IsNearlyZero(AlphaBottom))
		{
			DeltaRotation = (AlphaUp / AlphaBottom) * JacobianTranspose * EffectorDerivatives;

			// apply the result
			for (int32 Index = 0; Index < NumOfLinks; ++Index)
			{
				FVector BoneLocation = poseableMeshComp->GetBoneLocationByName(BoneNames[Index], EBoneSpaces::WorldSpace);
				FVector RotAxisX = poseableMeshComp->GetBoneAxis(BoneNames[Index], EAxis::X) - BoneLocation;
				FVector RotAxisY = poseableMeshComp->GetBoneAxis(BoneNames[Index], EAxis::Y) - BoneLocation;
				FVector RotAxisZ = poseableMeshComp->GetBoneAxis(BoneNames[Index], EAxis::Z) - BoneLocation;

				FQuat Quat = poseableMeshComp->GetBoneQuaternion(BoneNames[Index], EBoneSpaces::WorldSpace);

				if (RotAxisX.SizeSquared() > 0.f)
				{
					RotAxisX.Normalize();
					FQuat DeltaQuatX(RotAxisX, DeltaRotation(0));
					Quat = DeltaQuatX * Quat;
				}

				if (RotAxisY.SizeSquared() > 0.f)
				{
					RotAxisY.Normalize();
					FQuat DeltaQuatY(RotAxisY, DeltaRotation(1));
					Quat = DeltaQuatY * Quat;
				}

				if (RotAxisZ.SizeSquared() > 0.f)
				{
					RotAxisZ.Normalize();
					FQuat DeltaQuatZ(RotAxisZ, DeltaRotation(2));
					Quat = DeltaQuatZ * Quat;
				}
				poseableMeshComp->SetBoneRotationByName(BoneNames[Index], Quat.Rotator(), EBoneSpaces::WorldSpace);
			}
		}

		FRotator Rotator = poseableMeshComp->GetBoneRotationByName(BoneNames[0], EBoneSpaces::WorldSpace);
		GEngine->AddOnScreenDebugMessage(1, 0.1f, FColor::Red, FString::Printf(TEXT("%f"), Distance));
	}
}

void AIKModuleCharacter::SolveJacobianPinv(FName TipBoneName, FName RootBoneName, FVector TargetLocation, float Precision)
{
	// bone hierarchy에서 tip bone 위에 root bone이 있는지 확인
	FName BoneName = TipBoneName;
	TArray<FName> BoneNames;
	BoneNames.Add(BoneName);
	while (!BoneName.IsEqual(RootBoneName))
	{
		BoneName = poseableMeshComp->GetParentBone(BoneName);
		BoneNames.Insert(BoneName, 0);
	}
	if (BoneName.IsEqual("None"))
	{
		UE_LOG(LogTemp, Warning, TEXT("TipBoneName is NOT a child of RootBoneName"));
		return;
	}

	FVector TipBoneLocation = poseableMeshComp->GetBoneLocationByName(TipBoneName, EBoneSpaces::WorldSpace);
	float Distance = FVector::Dist(TipBoneLocation, TargetLocation);
	if (Distance > Precision)
	{
		// create Jacobian matrix
		Eigen::MatrixXf JacobianMat;
		int NumOfLinks = BoneNames.Num() - 1;
		JacobianMat.resize(3, NumOfLinks * 3);

		FVector TipLocation = poseableMeshComp->GetBoneLocationByName(TipBoneName, EBoneSpaces::WorldSpace);
		for (int32 Index = 0; Index < NumOfLinks; ++Index)
		{
			FVector BoneLocation = poseableMeshComp->GetBoneLocationByName(BoneNames[Index], EBoneSpaces::WorldSpace);

			FVector RotAxisX = poseableMeshComp->GetBoneAxis(BoneNames[Index], EAxis::X) - BoneLocation;
			FVector RotAxisY = poseableMeshComp->GetBoneAxis(BoneNames[Index], EAxis::Y) - BoneLocation;
			FVector RotAxisZ = poseableMeshComp->GetBoneAxis(BoneNames[Index], EAxis::Z) - BoneLocation;

			int ColumnOffset = Index * 3;

			if (RotAxisX.SizeSquared() > 0.f)
			{
				RotAxisX.Normalize();
				FVector DeltaX = FVector::CrossProduct(RotAxisX, TipLocation - BoneLocation);
				if (DeltaX.SizeSquared() > 0.f)
				{
					for (int Row = 0; Row < 3; ++Row)
						JacobianMat(Row, ColumnOffset) = DeltaX[Row];
				}
			}

			if (RotAxisY.SizeSquared() > 0.f)
			{
				RotAxisY.Normalize();
				FVector DeltaY = FVector::CrossProduct(RotAxisY, TipLocation - BoneLocation);
				if (DeltaY.SizeSquared() > 0.f)
				{
					DeltaY.Normalize();
					for (int Row = 0; Row < 3; ++Row)
						JacobianMat(Row, ColumnOffset + 1) = DeltaY[Row];
				}
			}

			if (RotAxisZ.SizeSquared() > 0.f)
			{
				RotAxisZ.Normalize();
				FVector DeltaZ = FVector::CrossProduct(RotAxisZ, TipLocation - BoneLocation);
				if (DeltaZ.SizeSquared() > 0.f)
				{
					for (int Row = 0; Row < 3; ++Row)
						JacobianMat(Row, ColumnOffset + 2) = DeltaZ[Row];
				}
			}
		}

		// calculate effector derivative
		Eigen::Vector3f TargetEigen, TipEigen;
		for (int i = 0; i < 3; ++i)
		{
			TargetEigen(i) = TargetLocation[i];
			TipEigen(i) = TipLocation[i];
		}
		Eigen::Vector3f EffectorDerivatives = TargetEigen - TipEigen;

		Eigen::MatrixXf JacobianSquare = JacobianMat.transpose() * JacobianMat;

		// Eigen::MatrixXf JacobianPinv = JacobianMat.completeOrthogonalDecomposition().pseudoInverse();
		Eigen::MatrixXf JacobianPinv = JacobianSquare.inverse() * JacobianMat.transpose();
		Eigen::MatrixXf DeltaRotation = JacobianPinv * EffectorDerivatives;

		// apply the result
		for (int32 Index = 0; Index < NumOfLinks; ++Index)
		{
			FVector BoneLocation = poseableMeshComp->GetBoneLocationByName(BoneNames[Index], EBoneSpaces::WorldSpace);
			FVector RotAxisX = poseableMeshComp->GetBoneAxis(BoneNames[Index], EAxis::X) - BoneLocation;
			FVector RotAxisY = poseableMeshComp->GetBoneAxis(BoneNames[Index], EAxis::Y) - BoneLocation;
			FVector RotAxisZ = poseableMeshComp->GetBoneAxis(BoneNames[Index], EAxis::Z) - BoneLocation;

			FQuat Quat = poseableMeshComp->GetBoneQuaternion(BoneNames[Index], EBoneSpaces::WorldSpace);

			if (RotAxisX.SizeSquared() > 0.f)
			{
				RotAxisX.Normalize();
				FQuat DeltaQuatX(RotAxisX, DeltaRotation(0));
				Quat = DeltaQuatX * Quat;
			}

			if (RotAxisY.SizeSquared() > 0.f)
			{
				RotAxisY.Normalize();
				FQuat DeltaQuatY(RotAxisY, DeltaRotation(1));
				Quat = DeltaQuatY * Quat;
			}

			if (RotAxisZ.SizeSquared() > 0.f)
			{
				RotAxisZ.Normalize();
				FQuat DeltaQuatZ(RotAxisZ, DeltaRotation(2));
				Quat = DeltaQuatZ * Quat;
			}
			Quat.Normalize();
			poseableMeshComp->SetBoneRotationByName(BoneNames[Index], Quat.Rotator(), EBoneSpaces::WorldSpace);
		}
	}
}