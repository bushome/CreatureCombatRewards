#pragma once

// Thin bridge to ArkShop's exported points API.
//
// ArkShop exports ArkShop::Points::AddPoints(int, const FString&) from ArkShop.dll (see Points.h in
// the ArkShop source). Instead of linking against ArkShop.lib, the function is looked up by its
// exported name at runtime. That keeps the build free of an ArkShop import library, lets this plugin
// load whether or not ArkShop has been loaded yet, and gives a clear log line (rather than a failed
// plugin load) if ArkShop is missing.

#include "API/ARK/Ark.h"

namespace CombatRewards::Shop
{
	// bool ArkShop::Points::AddPoints(int amount, const FString& eos_id), MSVC x64 decorated name.
	inline constexpr const char* kAddPointsSymbol = "?AddPoints@Points@ArkShop@@YA_NHAEBVFString@@@Z";
	inline constexpr const char* kShopModule = "ArkShop.dll";

	using AddPointsFn = bool(__cdecl*)(int, const FString&);

	enum class Status
	{
		Ready,
		ShopNotLoaded,
		SymbolNotFound
	};

	struct Api
	{
		Status status = Status::ShopNotLoaded;
		AddPointsFn add_points = nullptr;
	};

	/**
	 * \brief Looks up ArkShop's AddPoints. Cheap, and deliberately not cached between payouts:
	 * ArkShop can be hot-reloaded by AsaApi, which would leave a cached pointer dangling.
	 */
	inline Api Resolve()
	{
		Api api;

		const HMODULE module = GetModuleHandleA(kShopModule);
		if (module == nullptr)
		{
			api.status = Status::ShopNotLoaded;
			return api;
		}

		// Cast through void* so MSVC does not flag the FARPROC -> typed function pointer conversion (C4191).
		api.add_points = reinterpret_cast<AddPointsFn>(reinterpret_cast<void*>(GetProcAddress(module, kAddPointsSymbol)));
		api.status = api.add_points != nullptr ? Status::Ready : Status::SymbolNotFound;
		return api;
	}
} // namespace CombatRewards::Shop
