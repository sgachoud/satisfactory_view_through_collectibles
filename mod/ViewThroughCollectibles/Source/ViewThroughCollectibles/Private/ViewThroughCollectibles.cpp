// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewThroughCollectibles.h"
#include "VTCMapRepresentation.h"
#include "VTCOutlineSubsystem.h"
#include "FGMapManager.h"
#include "Engine/World.h"
#if !WITH_EDITOR && !UE_SERVER
#include "Patching/NativeHookManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogVTCMapHighlight, Log, All);

namespace
{
	bool IsLocalCollectibleMarker(const AFGMapManager* Manager, const FMapMarker& Marker)
	{
		const UWorld* World = Manager->GetWorld();
		const auto* Subsystem = World ? World->GetSubsystem<UVTCOutlineSubsystem>() : nullptr;
		return Subsystem && Subsystem->OwnsMapMarkerId(Marker.MarkerGUID);
	}
}
#endif

#define LOCTEXT_NAMESPACE "FViewThroughCollectiblesModule"

void FViewThroughCollectiblesModule::StartupModule()
{
#if !WITH_EDITOR && !UE_SERVER
	// The marker popup calls the map manager directly, bypassing the representation's
	// SetHighlighted override. Keep both its writes and reads local for our markers;
	// the server does not own these GUIDs, even when it supplies collectible data.
	SetMarkerHighlightHook = SUBSCRIBE_METHOD(AFGMapManager::SetMarkerHighlighted,
		[](auto& Scope, AFGMapManager* Manager, const FMapMarker& Marker, bool bHighlighted)
		{
			if (IsLocalCollectibleMarker(Manager, Marker))
			{
				Scope.Cancel();
				if (auto* Rep = Cast<UVTCMapRepresentation>(Manager->FindMapMarkerRepresentation(Marker)))
				{
					Rep->SetHighlighted(bHighlighted);
					UE_LOG(LogVTCMapHighlight, Display, TEXT("Collectible %s: local highlight %s"),
						*Marker.MarkerGUID.ToString(), bHighlighted ? TEXT("on") : TEXT("off"));
				}
			}
		});
	QueryRepresentationHighlightHook = SUBSCRIBE_METHOD(AFGMapManager::IsActorRepresentationHighlighted,
		[](auto& Scope, const AFGMapManager*, const UFGActorRepresentation* Representation,
			FLinearColor& OutColor, bool& bOutByLocalPlayer)
		{
			if (const auto* Rep = Cast<UVTCMapRepresentation>(Representation))
				Scope.Override(Rep->IsHighlighted(OutColor, bOutByLocalPlayer));
		});
	// These automatic dots are controlled by collectible state and client config.
	// The native popup's Apply/Remove actions must never send their GUIDs to the
	// server or turn them into saved, shared markers, including from a stale popup.
	UpdateLocalMarkerHook = SUBSCRIBE_METHOD(AFGMapManager::UpdateMapMarker,
		[](auto& Scope, AFGMapManager* Manager, const FMapMarker& Marker, bool)
		{
			if (IsLocalCollectibleMarker(Manager, Marker)) Scope.Cancel();
		});
	RemoveLocalMarkerHook = SUBSCRIBE_METHOD(AFGMapManager::RemoveMapMarker,
		[](auto& Scope, AFGMapManager* Manager, const FMapMarker& Marker)
		{
			if (IsLocalCollectibleMarker(Manager, Marker))
			{
				Scope.Cancel();
				if (auto* Rep = Cast<UVTCMapRepresentation>(Manager->FindMapMarkerRepresentation(Marker)))
					Rep->SetHighlighted(false);
			}
		});
#endif
}

void FViewThroughCollectiblesModule::ShutdownModule()
{
#if !WITH_EDITOR && !UE_SERVER
	if (SetMarkerHighlightHook.IsValid())
		UNSUBSCRIBE_METHOD(AFGMapManager::SetMarkerHighlighted, SetMarkerHighlightHook);
	if (QueryRepresentationHighlightHook.IsValid())
		UNSUBSCRIBE_METHOD(AFGMapManager::IsActorRepresentationHighlighted, QueryRepresentationHighlightHook);
	if (UpdateLocalMarkerHook.IsValid())
		UNSUBSCRIBE_METHOD(AFGMapManager::UpdateMapMarker, UpdateLocalMarkerHook);
	if (RemoveLocalMarkerHook.IsValid())
		UNSUBSCRIBE_METHOD(AFGMapManager::RemoveMapMarker, RemoveLocalMarkerHook);
#endif
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FViewThroughCollectiblesModule, ViewThroughCollectibles)
