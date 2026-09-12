// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

// Gallery streaming logs fire once per visible photo per frame. On Android every UE_LOG
// reaches logcat, which is expensive enough to show up in frame time, so the per-frame
// chatter lives at Verbose (compiled out of Shipping, off by default elsewhere) and only
// lifecycle events stay at Log.
DECLARE_LOG_CATEGORY_EXTERN(LogMemoryRoom, Log, All);

