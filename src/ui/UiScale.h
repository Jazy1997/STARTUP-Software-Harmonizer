#pragma once

// FR-59 (sessione 33) — la scala 70%-200% della finestra.
//
// IL MODELLO, in una riga: il layout non cambia mai forma, cambia solo quanto e'
// grande. Le tre schermate vengono disposte SEMPRE su uno spazio logico di
// 900x660 px, e un solo AffineTransform porta quello spazio alla dimensione
// fisica che l'utente ha chiesto. Nessuna costante di layout (rowHeight = 26,
// labelWidth = 60, l'altezza della tabella preset, i corpi dei font) sa che la
// scala esiste, ed e' esattamente il punto: restano scritte in pixel logici.
//
// Conseguenza scelta e non subita: il rapporto d'aspetto della finestra e'
// BLOCCATO. Trascinare l'angolo non fa piu' reflow, cambia la scala. Il reflow
// che si perde era solo orizzontale; verticalmente non era una funzione ma un
// difetto (a 620 px layoutEdit() chiedeva ~528 px su 522 disponibili e il
// toggle Keep Tails collassava ad altezza ~0, s.30). Con l'altezza logica
// fissa a 660 quel caso non esiste piu'.
//
// PERCHE' QUESTI NUMERI STANNO QUI E NON IN PluginEditor.cpp: cosi' si possono
// verificare senza aprire una DAW ne' linkare JUCE (vedi tests/ui_scale_test.cpp,
// stesso livello di CellInputParser.h e EmptyCellHold.h — D-11).
//
// ATTENZIONE ALLA SCALA DELL'HOST: quella non passa da qui. L'host applica il
// PROPRIO fattore (HiDPI/Retina) al transform dell'AudioProcessorEditor, e il
// nostro vive su un componente FIGLIO — i due si compongono. Vedi il commento in
// testa a HarmonizerAudioProcessorEditor::resized(): non e' un dettaglio di
// stile, e' l'unico modo che JUCE consente.
namespace ui
{
    // Lo spazio logico su cui lavorano resized() e le tre layout*(). E' la
    // dimensione che l'editor ha sempre avuto (il vecchio setSize(900, 660)),
    // quindi al 100% nulla si muove di un pixel rispetto a prima.
    inline constexpr int kLogicalWidth  = 900;
    inline constexpr int kLogicalHeight = 660;

    // FR-59, alla lettera.
    inline constexpr int kMinScalePercent = 70;
    inline constexpr int kMaxScalePercent = 200;

    // Le tacche offerte nel menu su Impostazioni. NON sono gli unici valori
    // possibili: trascinando l'angolo la scala e' continua, e il menu allora
    // mostra la percentuale reale invece di una voce selezionata (vedi
    // HarmonizerAudioProcessorEditor::syncScaleBoxFromState). Devono restare in
    // ordine crescente e dentro [kMinScalePercent, kMaxScalePercent] — la suite
    // ui_scale lo verifica.
    inline constexpr int kScalePresets[] = { 70, 85, 100, 125, 150, 200 };
    inline constexpr int kNumScalePresets = (int) (sizeof (kScalePresets) / sizeof (kScalePresets[0]));

    inline constexpr int kDefaultScalePercent = 100;

    // Difesa dell'unico ingresso non fidato: la percentuale arriva dallo stato
    // della sessione host, cioe' da XML che qualcuno puo' aver modificato o
    // troncato. Fuori range si riporta dentro, non si rifiuta.
    constexpr int clampScalePercent (int percent) noexcept
    {
        if (percent < kMinScalePercent) return kMinScalePercent;
        if (percent > kMaxScalePercent) return kMaxScalePercent;
        return percent;
    }

    constexpr int physicalWidthForPercent (int percent) noexcept
    {
        // 900 * p / 100 e' esatto per ogni p intero (9 px per punto
        // percentuale): l'arrotondamento qui non fa nulla, ma tiene le due
        // funzioni scritte allo stesso modo se la dimensione logica cambiasse.
        return (kLogicalWidth * clampScalePercent (percent) + 50) / 100;
    }

    constexpr int physicalHeightForPercent (int percent) noexcept
    {
        // 660 * p / 100 = 6.6 * p, qui l'arrotondamento serve davvero.
        return (kLogicalHeight * clampScalePercent (percent) + 50) / 100;
    }

    // L'inversa. La scala vera non e' un valore memorizzato ma la DIMENSIONE
    // ATTUALE della finestra: dopo un trascinamento dell'angolo l'unica fonte
    // di verita' e' getWidth(), e questa funzione la traduce nella percentuale
    // da mostrare e da salvare.
    //
    // Si parte dalla larghezza e non dall'altezza perche' i 9 px per punto
    // percentuale della larghezza rendono la mappa iniettiva su tutto
    // [70, 200]: percent -> larghezza -> percent torna sempre al valore di
    // partenza (verificato in ui_scale_test). Sull'altezza, a 6.6 px per punto,
    // non varrebbe.
    constexpr int scalePercentFromWidth (int physicalWidth) noexcept
    {
        if (physicalWidth <= 0)
            return kMinScalePercent;

        return clampScalePercent ((physicalWidth * 100 + kLogicalWidth / 2) / kLogicalWidth);
    }
}
