#include "engine/data/i18n.h"

namespace mythos {
namespace {

// Strings of the engine's own screens (the settings page), so a game gets them without
// writing them. A game's own table wins for any key it defines too.
const TranslationTable kEnglish = {
    {"settings.title", "SETTINGS"},
    {"settings.graphics", "GRAPHICS"},
    {"settings.resolution", "RESOLUTION"},
    {"settings.desktop", "DESKTOP"},
    {"settings.renderScale", "RENDER SCALE"},
    {"settings.low", "LOW"},
    {"settings.medium", "MEDIUM"},
    {"settings.high", "HIGH"},
    {"settings.veryLow", "VERY LOW"},
    {"settings.veryHigh", "VERY HIGH"},
    {"settings.ultra", "ULTRA"},
    {"settings.max", "MAX"},
    {"settings.ultraMax", "ULTRA MAX"},
    {"settings.filmGrain", "FILM GRAIN"},
    {"settings.fullscreen", "FULLSCREEN"},
    {"settings.vsync", "VSYNC"},
    {"settings.antiAliasing", "ANTI-ALIASING"},
    {"settings.fpsCap", "FRAME RATE LIMIT"},
    {"settings.unlimited", "UNLIMITED"},
    {"settings.shadows", "SHADOWS"},
    {"settings.materials", "MATERIALS"},
    {"settings.basic", "BASIC"},
    {"settings.full", "FULL"},
    {"settings.motionBlur", "MOTION BLUR"},
    {"settings.cameraEffect", "CAMERA EFFECT"},
    {"settings.audio", "AUDIO"},
    {"settings.masterVolume", "MASTER VOLUME"},
    {"settings.ambience", "AMBIENCE"},
    {"settings.effects", "EFFECTS"},
    {"settings.language", "LANGUAGE"},
    {"settings.back", "BACK"},
    {"settings.hint", "W/S SELECT   A/D CHANGE   ESC BACK"},
    {"common.on", "ON"},
    {"common.off", "OFF"},
};

const TranslationTable kGreek = {
    {"settings.title", "ΡΥΘΜΙΣΕΙΣ"},
    {"settings.graphics", "ΓΡΑΦΙΚΑ"},
    {"settings.resolution", "ΑΝΑΛΥΣΗ"},
    {"settings.desktop", "ΟΠΩΣ Η ΟΘΟΝΗ"},
    {"settings.renderScale", "ΚΛΙΜΑΚΑ ΑΠΟΔΟΣΗΣ"},
    {"settings.low", "ΧΑΜΗΛΗ"},
    {"settings.medium", "ΜΕΣΑΙΑ"},
    {"settings.high", "ΥΨΗΛΗ"},
    {"settings.veryLow", "ΠΟΛΥ ΧΑΜΗΛΗ"},
    {"settings.veryHigh", "ΠΟΛΥ ΥΨΗΛΗ"},
    {"settings.ultra", "ΥΠΕΡ"},
    {"settings.max", "ΜΕΓΙΣΤΗ"},
    {"settings.ultraMax", "ΥΠΕΡ ΜΕΓΙΣΤΗ"},
    {"settings.filmGrain", "ΚΟΚΚΟΣ ΦΙΛΜ"},
    {"settings.fullscreen", "ΠΛΗΡΗΣ ΟΘΟΝΗ"},
    {"settings.vsync", "VSYNC"},
    {"settings.antiAliasing", "ΕΞΟΜΑΛΥΝΣΗ ΑΚΜΩΝ"},
    {"settings.fpsCap", "ΟΡΙΟ ΚΑΡΕ"},
    {"settings.unlimited", "ΑΠΕΡΙΟΡΙΣΤΟ"},
    {"settings.shadows", "ΣΚΙΕΣ"},
    {"settings.materials", "ΥΛΙΚΑ"},
    {"settings.basic", "ΒΑΣΙΚΑ"},
    {"settings.full", "ΠΛΗΡΗ"},
    {"settings.motionBlur", "ΘΟΛΩΜΑ ΚΙΝΗΣΗΣ"},
    {"settings.cameraEffect", "ΕΦΕ ΚΑΜΕΡΑΣ"},
    {"settings.audio", "ΗΧΟΣ"},
    {"settings.masterVolume", "ΓΕΝΙΚΗ ΕΝΤΑΣΗ"},
    {"settings.ambience", "ΑΤΜΟΣΦΑΙΡΑ"},
    {"settings.effects", "ΕΦΕ"},
    {"settings.language", "ΓΛΩΣΣΑ"},
    {"settings.back", "ΠΙΣΩ"},
    {"settings.hint", "W/S ΕΠΙΛΟΓΗ   A/D ΑΛΛΑΓΗ   ESC ΠΙΣΩ"},
    {"common.on", "ΝΑΙ"},
    {"common.off", "ΟΧΙ"},
};

}  // namespace

const TranslationTable& engineStrings(Lang l) { return l == Lang::Greek ? kGreek : kEnglish; }

}  // namespace mythos
