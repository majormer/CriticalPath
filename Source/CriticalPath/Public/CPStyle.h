// Copyright (c) 2026-present Finalomega Labs. All rights reserved. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "CPEngineTypes.h"
#include "Engine/Font.h"
#include "Fonts/SlateFontInfo.h"

/**
 * The design system (docs/DesignSystem.md) as code — the single owner of palette, status
 * tokens, typography, and spacing. Feature widgets never hard-code colours or sizes.
 */
namespace CPStyle
{
	// ---- Palette (DesignSystem.md tokens) ----------------------------------------------------
	inline const FLinearColor Backdrop(0.043f, 0.055f, 0.075f, 0.97f);
	inline const FLinearColor Surface(0.078f, 0.098f, 0.133f, 1.0f);
	inline const FLinearColor Row(0.106f, 0.133f, 0.188f, 1.0f);
	inline const FLinearColor RowNested(0.086f, 0.108f, 0.153f, 1.0f);
	inline const FLinearColor Divider(0.165f, 0.200f, 0.255f, 1.0f);
	inline const FLinearColor Accent(0.247f, 0.714f, 0.788f, 1.0f);
	inline const FLinearColor TextPrimary(0.910f, 0.929f, 0.949f, 1.0f);
	inline const FLinearColor TextSecondary(0.604f, 0.655f, 0.706f, 1.0f);

	// Status colours
	inline const FLinearColor StatusGreen(0.298f, 0.686f, 0.427f, 1.0f);
	inline const FLinearColor StatusBlue(0.310f, 0.639f, 0.890f, 1.0f);
	inline const FLinearColor StatusAmber(0.878f, 0.639f, 0.243f, 1.0f);
	inline const FLinearColor StatusRed(0.878f, 0.424f, 0.376f, 1.0f);
	inline const FLinearColor StatusViolet(0.608f, 0.545f, 0.839f, 1.0f);
	/** Benign/working-as-intended: visible but not calling for attention. */
	inline const FLinearColor StatusNeutral(0.478f, 0.529f, 0.592f, 1.0f);
	// ---- Meter series (Balance four-rate bars) -----------------------------------------------
	// These four bars are four MEASURES OF ONE QUANTITY, so their colours carry identity, not
	// judgement. Status hues are deliberately excluded: a short StatusGreen "current output" bar
	// reads as healthy when it is precisely the problem, and spending status colours on routine
	// data devalues them where they do mean something.
	//
	// Hues are taken from Satisfactory's own art direction so the panel reads as part of the game
	// rather than a web dashboard: FICSIT orange, the FICSIT mug blue, crystal violet, and a warm
	// equipment tan. Validated with the palette checker against the dark meter track -- worst
	// adjacent pair protan dE 13.7 / tritan 15.8 / normal-vision 19.4. The previous set failed at
	// 5.3 / 2.5 / 6.6: built capacity and consumer need were indistinguishable even with full
	// colour vision.
	//
	// ORDER IS LOAD-BEARING. Bars render need -> capacity -> output -> use, so those are the
	// adjacent pairs that must separate. A cool grey reference beside a blue capacity bar fails
	// every time (dE ~10 normal) no matter the shade, which is why the reference is warm; and
	// blue beside violet collapses to dE 0.5 under deuteranopia, so orange sits between them.
	inline const FLinearColor MeterReference(0.604f, 0.468f, 0.337f); // #ccb69d - a target, kept recessive
	inline const FLinearColor MeterCapacity(0.048f, 0.361f, 0.652f);  // #3ea2d3 built capacity
	inline const FLinearColor MeterOutput(0.982f, 0.296f, 0.019f);    // #fd9426 current output (FICSIT orange)
	inline const FLinearColor MeterUse(0.445f, 0.080f, 0.631f);       // #b250d0 current use
	inline const FLinearColor MeterTrack(0.055f, 0.070f, 0.100f);

	// Three verdict bands, not two. Research-gated progress is the NORMAL state for most of a
	// playthrough -- painting it red means the panel shouts "BLOCKED" from the first hour to the
	// last, and a warning that is always on is not a warning. Red is reserved for a factory fault
	// the player can actually go fix; waiting on the M.A.M./HUB is informational blue.
	inline const FLinearColor VerdictBlockedBand(0.17f, 0.075f, 0.075f, 1.0f);
	inline const FLinearColor VerdictNextBand(0.065f, 0.105f, 0.155f, 1.0f);
	inline const FLinearColor VerdictReadyBand(0.06f, 0.13f, 0.09f, 1.0f);

	// ---- Spacing -----------------------------------------------------------------------------
	// Two side-by-side section columns need the extra width.
	inline constexpr float PanelMaxWidth = 1080.0f;
	inline constexpr float PanelMaxHeight = 860.0f;
	/** Wrap width for notice text inside one half-width column (minus paddings/glyph). */
	inline constexpr float ColumnNoticeWrap = (PanelMaxWidth - 24.0f) * 0.5f - 40.0f;
	inline constexpr float IndentPerDepth = 18.0f;

	// ---- Typography --------------------------------------------------------------------------
	// The in-game RUNTIME multi-script font (never the offline FactoryFont — no shaping, tofu
	// for Arabic/Persian/Thai). Falls back to engine Roboto so text is never invisible.
	inline FSlateFontInfo Font(int32 Size, bool bBold = false)
	{
		static TWeakObjectPtr<UFont> Cached;
		UFont* UIFont = Cached.Get();
		if (!UIFont)
		{
			UIFont = LoadObject<UFont>(nullptr, TEXT("/Game/FactoryGame/Interface/Font/DescriptionText.DescriptionText"));
			Cached = UIFont;
		}

		FSlateFontInfo Info;
		Info.Size = Size;
		if (UIFont)
		{
			Info.FontObject = UIFont;
			// NAME_None keeps the composite's default typeface + script-fallback chain intact.
			// (A named typeface may bypass fallback for non-Latin text; bold is therefore only
			// requested, and non-Latin scripts silently get regular weight — acceptable.)
			Info.TypefaceFontName = bBold ? FName(TEXT("Bold")) : NAME_None;
		}
		else
		{
			Info.FontObject = LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto"));
			Info.TypefaceFontName = bBold ? FName(TEXT("Bold")) : FName(TEXT("Regular"));
		}
		return Info;
	}

	// ---- Status icons (game monochrome textures — white, tinted with StatusColor) --------------
	// Maintainer-picked from the in-game evaluation strip (DesignSystem.md status-icon table).
	inline const TCHAR* IconProducing = TEXT("/Game/FactoryGame/Interface/UI/Assets/MonochromeIcons/TXUI_MIcon_Cogwheel"); // rotating
	inline const TCHAR* IconAlert     = TEXT("/Game/FactoryGame/Interface/UI/Assets/MonochromeIcons/TXUI_MIcon_Warning");
	inline const TCHAR* IconBanked    = TEXT("/Game/FactoryGame/Interface/UI/Assets/MonochromeIcons/TXUI_MIcon_SortRule_Overflow");
	inline const TCHAR* IconSlow      = TEXT("/Game/FactoryGame/Interface/UI/Assets/MonochromeIcons/TXUI_MIcon_Speedometer");
	inline const TCHAR* IconResearch  = TEXT("/Game/FactoryGame/Interface/UI/Assets/MonochromeIcons/TXUI_MIcon_QuestionMark");
	inline const TCHAR* IconDelivered = TEXT("/Game/FactoryGame/Interface/UI/Assets/MonochromeIcons/TXUI_MIcon_ThumbUp");

	// ---- Status tokens -------------------------------------------------------------------------
	// Status renders as a coloured left-edge stripe + explicit status TEXT on the row. The old
	// glyph set (■●▲×◆) was dropped: shapes need a legend and their mixed widths broke column
	// alignment. Colour alone never carries status — the row text always names it.
	// Semantic model: GREEN done · BLUE banked (act: deliver) · AMBER degraded-but-flowing ·
	// RED hard stop · VIOLET research needed · NEUTRAL working as intended.
	inline FLinearColor StatusColor(ECPNodeStatus Status)
	{
		switch (Status)
		{
		case ECPNodeStatus::Delivered:      return StatusGreen;
		case ECPNodeStatus::ReadyToDeliver: return StatusBlue;
		case ECPNodeStatus::UnderSupplied:  return StatusAmber;
		case ECPNodeStatus::Blocked:        return StatusRed;
		case ECPNodeStatus::RecipeLocked:   return StatusViolet;
		case ECPNodeStatus::Fulfilled:      return StatusNeutral;
		case ECPNodeStatus::TimeLimited:    return StatusNeutral;
		case ECPNodeStatus::Saturated:      return StatusNeutral;
		case ECPNodeStatus::StandbyReserve: return StatusNeutral;
		case ECPNodeStatus::Extraction:     return StatusNeutral;
		default:                            return TextSecondary;
		}
	}
}
