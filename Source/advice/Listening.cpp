#include "Advisor.h"
#include <map>

// What each finding sounds like, and when it is a choice rather than a mistake.
// The numbers say where to look; these say what to listen for, so the ears learn
// to hear it and can overrule the meter.

namespace wwma
{
namespace
{
    struct Ears
    {
        const char *listenEn, *listenFi, *ignoreEn, *ignoreFi;
    };

    const std::map<std::string, Ears>& table()
    {
        static const std::map<std::string, Ears> t {
            // ---- tone
            { "range0+", { "On good headphones the low end feels heavy and woolly, as if the mix sat on a cushion. In a car it booms.",
                           "Hyvillä kuulokkeilla bassopää tuntuu raskaalta ja villaiselta, kuin miksi istuisi tyynyn päällä. Autossa se jumputtaa.",
                           "Dubstep, trap and other bass music often carry more sub on purpose. If a reference you love has as much, it is the style.",
                           "Dubstepissä, trapissa ja muussa bassomusiikissa subia on usein tarkoituksella enemmän. Jos rakastamassasi referenssissä on yhtä paljon, se on tyyliä." } },
            { "range0-", { "On headphones the kick hits but you don't feel it: the drop sounds like it has no floor.",
                           "Kuulokkeilla kick lyö, mutta sitä ei tunne: drop kuulostaa siltä, ettei sillä ole lattiaa.",
                           "Acoustic, lo-fi and many rock mixes keep the sub light on purpose.",
                           "Akustisessa, lo-fissä ja monessa rockmiksissä subi pidetään tarkoituksella kevyenä." } },
            { "range1+", { "Bass notes swell and blur into each other, and the kick sounds like a thud rather than a hit.",
                           "Bassonuotit paisuvat ja sulautuvat toisiinsa, ja kick kuulostaa tömähdykseltä eikä iskulta.",
                           "Warm, bass-heavy styles like deep house or hip hop live here. Compare with a reference in the same style.",
                           "Lämpimät, bassovoittoiset tyylit kuten deep house tai hip hop elävät täällä. Vertaa saman tyylin referenssiin." } },
            { "range1-", { "At low volume the bass line almost disappears and the groove feels light.",
                           "Hiljaisella bassolinja lähes katoaa ja groove tuntuu kevyeltä.",
                           "Bright, sparse arrangements can be light here on purpose.",
                           "Kirkkaat, väljät sovitukset voivat olla tällä alueella tarkoituksella kevyitä." } },
            { "range2+", { "The mix sounds like it plays inside a cardboard box: muffled and crowded, and the snare and vocal don't come forward.",
                           "Miksi kuulostaa kuin se soisi pahvilaatikossa: tukkoiselta ja täydeltä, eivätkä virveli ja vokaali tule eteen.",
                           "A warm, full, analogue sound is sometimes the point, as in lo-fi, soul or downtempo.",
                           "Lämmin, täyteläinen, analoginen soundi on joskus koko idea, kuten lo-fissä, soulissa tai downtempossa." } },
            { "range2-", { "The snare and the vocal sound thin, like through a small radio, and the mix lacks warmth.",
                           "Virveli ja vokaali kuulostavat ohuilta, kuin pienestä radiosta, ja miksistä puuttuu lämpö.",
                           "Very clean, modern, bright productions can be lean here.",
                           "Hyvin puhtaat, modernit ja kirkkaat tuotannot voivat olla tällä alueella laihoja." } },
            { "range3+", { "It sounds nasal or honky, like through a phone, and after a while it gets tiring.",
                           "Se kuulostaa nenäkkäältä tai tuuttaavalta, kuin puhelimen läpi, ja hetken päästä se väsyttää.",
                           "Guitar music and vocal-led mixes are often forward here: that is where they speak.",
                           "Kitaramusiikki ja vokaalivetoiset miksit ovat tällä alueella usein edessä: siellä ne puhuvat." } },
            { "range3-", { "Big on monitors, but on a phone or laptop the lead and the vocal fall back and the middle of the mix feels empty.",
                           "Isolta monitoreista, mutta puhelimesta tai läppäristä liidi ja vokaali painuvat taakse ja miksin keskusta tuntuu tyhjältä.",
                           "Some club tracks scoop the mids on purpose for a big, open drop. Check that it still works on a phone.",
                           "Jotkut klubibiisit kuopaavat keskiääniä tarkoituksella isoa, avointa droppia varten. Tarkista, että se toimii yhä puhelimesta." } },
            { "range4+", { "Turn it up a little: it starts to sting and you want to turn it down. Hats and synths feel sharp.",
                           "Nosta vähän voimakkuutta: se alkaa pistellä ja tekee mieli hiljentää. Hihatit ja syntikat tuntuvat teräviltä.",
                           "Aggressive styles like hard techno, hardstyle or rock are forward here on purpose. It should feel exciting, not painful.",
                           "Aggressiiviset tyylit kuten hard techno, hardstyle tai rock ovat tällä alueella tarkoituksella edessä. Sen pitää tuntua jännittävältä, ei kivuliaalta." } },
            { "range4-", { "Everything sounds distant and polite, and at low volume the vocal and the snare lose their edge.",
                           "Kaikki kuulostaa kaukaiselta ja kohteliaalta, ja hiljaisella vokaali ja virveli menettävät terävyytensä.",
                           "Dark, mellow styles like ambient, lo-fi or deep house sit back here on purpose.",
                           "Tummat, pehmeät tyylit kuten ambient, lo-fi tai deep house ovat tällä alueella tarkoituksella taaempana." } },
            { "range5+", { "Hats and S sounds hiss, noise and reverb tails stand out, and it gets tiring on headphones.",
                           "Hihatit ja s-äänteet sihisevät, kohina ja kaikujen hännät erottuvat, ja kuulokkeilla se väsyttää.",
                           "Airy, bright pop and EDM masters are often brighter than average.",
                           "Ilmavat, kirkkaat pop- ja EDM-masterit ovat usein keskimääräistä kirkkaampia." } },
            { "range5-", { "The mix sounds dull, like a blanket over the speakers, and next to other tracks it sounds old.",
                           "Miksi kuulostaa tunkkaiselta, kuin kaiuttimien päällä olisi peitto, ja muiden biisien rinnalla se kuulostaa vanhalta.",
                           "Lo-fi, dub and vintage sounds roll off the top on purpose.",
                           "Lo-fi, dub ja vintage-soundit pyöristävät diskanttia tarkoituksella." } },

            // ---- loudness and dynamics
            { "loud", { "Match the loudness of a reference: does yours sound flatter, with a smaller kick?",
                        "Tasaa äänekkyys referenssin kanssa: kuulostaako omasi litteämmältä ja kick pienemmältä?",
                        "Club and hard styles are mastered loud on purpose. If the punch is still there at matched loudness, it is a choice.",
                        "Klubi- ja kovat tyylit masteroidaan tarkoituksella koviksi. Jos isku on tallella tasatulla äänekkyydellä, se on valinta." } },
            { "quiet", { "Play it right after a released track in the same style, without loudness normalisation: yours sounds weaker.",
                         "Soita se heti julkaistun saman tyylin biisin perään ilman normalisointia: omasi kuulostaa heikommalta.",
                         "For streaming only, a quieter master is fine: it is turned down to about -14 LUFS anyway.",
                         "Pelkkään striimaukseen hiljaisempi master on ok: se lasketaan joka tapauksessa noin −14 LUFS:iin." } },
            { "peak", { "Usually you hear nothing until the file becomes MP3 or AAC, or plays over Bluetooth: then the loudest hits crackle.",
                        "Yleensä et kuule mitään, ennen kuin tiedostosta tulee MP3 tai AAC tai se soi Bluetoothin yli: silloin kovimmat iskut rätisevät.",
                        "", "" } },
            { "squashed", { "The kick and the snare no longer jump out, everything is equally loud, and turning it up doesn't make it more exciting.",
                            "Kick ja virveli eivät enää hyppää esiin, kaikki on yhtä kovaa, eikä voimakkuuden nostaminen tee siitä jännittävämpää.",
                            "Some styles (hardstyle, big room, some trap) are squashed on purpose. Compare with a reference at the same loudness.",
                            "Jotkut tyylit (hardstyle, big room, osa trapista) puristetaan tarkoituksella. Vertaa referenssiin samalla äänekkyydellä." } },
            { "dynamic", { "In a car or on a phone, the quiet parts and the details get lost and you keep reaching for the volume.",
                           "Autossa tai puhelimesta hiljaiset kohdat ja yksityiskohdat katoavat, ja käsi hakeutuu koko ajan voimakkuussäätimelle.",
                           "Classical, jazz, ambient and film music should be dynamic.",
                           "Klassisen, jazzin, ambientin ja elokuvamusiikin kuuluukin olla dynaamista." } },
            { "lra", { "Set the volume in the loudest part, then listen to the quietest: does it vanish?",
                       "Säädä voimakkuus kovimmassa kohdassa ja kuuntele sitten hiljaisin: katoaako se?",
                       "A big drop after a quiet breakdown is part of the music; only the size of the jump matters.",
                       "Iso drop hiljaisen breakdownin jälkeen kuuluu musiikkiin; vain hypyn koko ratkaisee." } },

            // ---- small speakers and stereo
            { "phonebass", { "Play it on your phone's speaker: can you still follow the bass line and the kick?",
                             "Soita se puhelimen kaiuttimesta: pystytkö yhä seuraamaan bassolinjaa ja kickiä?",
                             "", "" } },
            { "lowwidth", { "Switch to mono: does the bass get weaker or change tone? On headphones the bass can feel beside you rather than in front.",
                            "Kytke mono: heikkeneekö basso tai muuttuuko sen sävy? Kuulokkeilla basso voi tuntua olevan sivulla eikä edessä.",
                            "", "" } },
            { "phase", { "Switch to mono: something disappears, or turns hollow and phasey.",
                         "Kytke mono: jokin katoaa tai muuttuu ontoksi ja faasiseksi.",
                         "", "" } },
            { "monoloss", { "Switch to mono: a sound around that frequency drops back or disappears.",
                            "Kytke mono: jokin ääni tuon taajuuden ympärillä painuu taakse tai katoaa.",
                            "Wide pads and effects can lose a little in mono. The lead, the vocal and the drums must stay.",
                            "Leveät padit ja efektit saavat hävitä monona vähän. Liidin, vokaalin ja rumpujen pitää pysyä." } },
            { "wide", { "Switch to mono: the balance changes and the wide parts drop back.",
                        "Kytke mono: tasapaino muuttuu ja leveät osat painuvat taakse.",
                        "Ambient and chill music can be very wide on purpose. Check the core parts in mono.",
                        "Ambient ja chill voivat olla tarkoituksella hyvin leveitä. Tarkista ydinosat monona." } },
            { "narrow", { "On headphones the mix sits in the middle of your head and nothing feels around you.",
                          "Kuulokkeilla miksi on keskellä päätä, eikä mikään tunnu olevan ympärillä.",
                          "Old-school, lo-fi and mono-minded styles can be narrow on purpose.",
                          "Old school, lo-fi ja monohenkiset tyylit voivat olla tarkoituksella kapeita." } },
            { "balance", { "On headphones your attention pulls to one side. Listen to the left and the right in turn.",
                           "Kuulokkeilla huomio vetää toiselle puolelle. Kuuntele vasenta ja oikeaa vuorotellen.",
                           "A short moment can lean on purpose, like a panned fill; the whole mix shouldn't.",
                           "Lyhyt hetki saa kallistua tarkoituksella, kuten panoroitu fill; koko miksi ei." } },
            { "sidehigh", { "Switch to mono: the shine and the air disappear and the top end turns dull.",
                            "Kytke mono: kiilto ja ilma katoavat ja diskantti tylsistyy.",
                            "", "" } },

            // ---- faults
            { "dcoffset", { "You rarely hear it; at most a click at cuts and edits.",
                            "Sitä kuulee harvoin; korkeintaan naksahduksena leikkauksissa.",
                            "", "" } },
            { "clipping", { "A crackle or a harsh, crunchy edge on the loudest hits (kick, snare, drops), worse after MP3.",
                            "Rätinää tai kireää, rouheaa reunaa kovimmissa iskuissa (kick, virveli, dropit), MP3:n jälkeen vielä pahempana.",
                            "A soft clipper used on purpose is fine if you can't hear it in A/B.",
                            "Tarkoituksella käytetty soft clipper on ok, jos et kuule sitä A/B-vertailussa." } },
            { "headroom", { "Nothing to hear yet: it matters at the mastering stage.",
                            "Tässä ei vielä kuule mitään: tämä ratkaisee vasta masteroinnissa.",
                            "If this is the finished master and not a mix for mastering, it doesn't apply.",
                            "Jos tämä on valmis master eikä masterointiin lähtevä miksi, tämä ei koske sinua." } },
            { "hotmix", { "Bypass the limiter at matched level: underneath, the mix probably has a different balance.",
                          "Ohita limitteri tasatulla tasolla: sen alla miksin tasapaino on luultavasti erilainen.",
                          "Mixing into gentle bus compression on purpose is a valid way to work.",
                          "Tarkoituksellinen miksaus kevyen väyläkompression läpi on ihan hyvä tapa työskennellä." } },
            { "rumble", { "You mostly don't hear it, but the speaker cones move and the limiter works harder.",
                          "Sitä ei yleensä kuule, mutta kaiutinelementit liikkuvat ja limitteri tekee enemmän töitä.",
                          "", "" } },

            // ---- drums and resonances
            { "kickbass", { "In the drop, can you hear each kick start, or is the low end one steady roar?",
                            "Kuuletko dropissa jokaisen kickin alun, vai onko bassopää yhtä tasaista jyrinää?",
                            "Dub and some techno blend kick and bass into one low end on purpose.",
                            "Dubissa ja osassa teknoa kick ja basso sulautetaan tarkoituksella yhdeksi bassopääksi." } },
            { "flatdrums", { "The snare and the hats sound pasted on, without snap, and turning up doesn't make them hit.",
                             "Virveli ja hihatit kuulostavat liimatuilta, ilman napakkuutta, eikä voimakkuuden nostaminen saa niitä iskemään.",
                             "Smooth, soft-hitting styles like lo-fi or ambient don't need sharp drums.",
                             "Pehmeät tyylit kuten lo-fi tai ambient eivät tarvitse teräviä rumpuja." } },
            { "spikydrums", { "The snare and the hats jump out of the mix and poke your ears, most of all on small speakers.",
                              "Virveli ja hihatit hyppäävät irti miksistä ja pistävät korvaan, eniten pienistä kaiuttimista.",
                              "Breaks and hip hop often like dry, punchy drums up front.",
                              "Breaksissä ja hip hopissa kuivat, iskevät rummut ovat usein tarkoituksella edessä." } },
            { "boomnote", { "Play the bass line: one note is clearly louder than the others and booms.",
                            "Soita bassolinja: yksi nuotti on selvästi muita kovempi ja jumputtaa.",
                            "", "" } },
            { "piercing", { "One whistling tone sticks out of the top and hurts when you turn it up.",
                            "Yksi vihlova sävy nousee diskantista esiin ja sattuu, kun voimakkuutta nostaa.",
                            "", "" } },
            { "sibilance", { "S and T sounds hiss or spit, most of all on headphones.",
                             "S- ja t-äänteet sihisevät tai sylkevät, eniten kuulokkeilla.",
                             "", "" } },
            { "notch", { "Something feels missing in one spot: a hollow, unfinished sound.",
                         "Jossain kohdassa tuntuu puuttuvan jotain: ontto, keskeneräinen soundi.",
                         "A deliberate cut that makes room for a vocal can be right.",
                         "Tarkoituksellinen leikkaus, joka tekee tilaa vokaalille, voi olla oikein." } },

            // ---- track mode
            { "kick-low", { "On a phone or laptop the kick almost disappears; on big speakers it is a rumble more than a hit.",
                            "Puhelimesta tai läppäristä kick lähes katoaa; isoista kaiuttimista se on enemmän jyrinää kuin isku.",
                            "Deep, sub-heavy styles tune kicks low on purpose, as long as the click carries on small speakers.",
                            "Syvissä, subipainotteisissa tyyleissä kick viritetään tarkoituksella matalalle, kunhan klikki kantaa pienistä kaiuttimista." } },
            { "kick-high", { "The kick knocks like on a door instead of thumping in your chest.",
                             "Kick koputtaa kuin oveen sen sijaan, että se tömähtäisi rintaan.",
                             "Some styles (breaks, garage, lo-fi) like a tighter, higher kick.",
                             "Jotkut tyylit (breaks, garage, lo-fi) suosivat tiukempaa, korkeampaa kickiä." } },
            { "kick-long", { "The kick's tail and the bass smear into one boom; the bass notes after the kick lose their start.",
                             "Kickin häntä ja basso sulavat yhdeksi jumahdukseksi; kickin jälkeiset bassonuotit menettävät alkunsa.",
                             "Without an off-beat bass, a long boomy kick can be the whole point (hardstyle, some techno).",
                             "Ilman off-beat-bassoa pitkä jumahtava kick voi olla koko idea (hardstyle, osa teknosta)." } },
            { "kick-short", { "The kick clicks but has no weight: it ticks rather than thumps.",
                              "Kick klikkaa, mutta siinä ei ole painoa: se tikittää eikä tömähdä.",
                              "Minimal and some techno use short, clicky kicks on purpose.",
                              "Minimalissa ja osassa teknoa käytetään tarkoituksella lyhyitä, klikkaavia kickejä." } },
            { "kick-noclick", { "Play the track on your phone: the beat disappears.",
                                "Soita raita puhelimesta: biitti katoaa.",
                                "", "" } },
            { "kick-clicky", { "On earbuds every kick ticks sharply in your ear.",
                               "Nappikuulokkeilla jokainen kick tikittää terävästi korvaan.",
                               "Hard styles like a sharp click on purpose.",
                               "Kovissa tyyleissä terävä klikki on tarkoituksellinen." } },
            { "kick-mud", { "The kick sounds like it hits cardboard, and it covers the snare and the bass harmonics.",
                            "Kick kuulostaa siltä kuin se osuisi pahviin, ja se peittää virvelin ja basson harmoniset.",
                            "", "" } },
            { "kick-squashed", { "The kick is loud but flat: it doesn't punch, it just presses.",
                                 "Kick on kova mutta litteä: se ei iske, se vain painaa.",
                                 "A clipped, distorted kick is part of hard styles.",
                                 "Klipattu, säröinen kick kuuluu koviin tyyleihin." } },
            { "bass-noharm", { "Play it on your phone: the bass line is gone, only the rest of the mix is left.",
                               "Soita puhelimesta: bassolinja on poissa, jäljellä on vain muu miksi.",
                               "", "" } },
            { "bass-fizzy", { "The bass buzzes in the middle of the mix and gets in the way of the chords and the vocal.",
                              "Basso surisee miksin keskellä ja menee sointujen ja vokaalin tielle.",
                              "Mid-range basses (reese, growl) are the lead in some styles.",
                              "Keskialueen bassot (reese, growl) ovat joissain tyyleissä itse pääosa." } },
            { "bass-sub+", { "On big speakers the bass is felt more than heard, and the notes are hard to follow.",
                             "Isoista kaiuttimista bassoa tuntee enemmän kuin kuulee, eikä nuotteja pysty seuraamaan.",
                             "A pure sub bass under a busy mix is normal in trap and dubstep.",
                             "Puhdas subibasso täyden miksin alla on normaalia trapissa ja dubstepissä." } },
            { "bass-uneven", { "Some bass notes jump out and others vanish.",
                               "Jotkut bassonuotit hyppäävät esiin ja toiset katoavat.",
                               "", "" } },
            { "bus-sub+", { "The low end booms and feels heavy, most of all in a car.",
                            "Bassopää jumputtaa ja tuntuu raskaalta, eniten autossa.",
                            "Bass music carries more sub on purpose.",
                            "Bassomusiikissa subia on tarkoituksella enemmän." } },
            { "bus-sub-", { "On a big system the low end feels thin.",
                            "Isosta järjestelmästä bassopää tuntuu ohuelta.",
                            "", "" } },
            { "bus-squashed", { "The groove feels flat: the kick doesn't rise above the bass.",
                                "Groove tuntuu litteältä: kick ei nouse basson yläpuolelle.",
                                "", "" } },
            { "clash", { "When kick and bass play together they turn into one boom, and neither is clear.",
                         "Kun kick ja basso soivat yhtä aikaa, ne muuttuvat yhdeksi jumahdukseksi, eikä kumpikaan ole selkeä.",
                         "", "" } },
            { "kick-tune", { "The kick and the bass line sound slightly out of tune with each other.",
                             "Kick ja bassolinja kuulostavat vähän epävireisiltä keskenään.",
                             "An atonal kick in a busy mix can be fine if it doesn't stick out.",
                             "Atonaalinen kick täydessä miksissä voi olla ok, jos se ei pistä esiin." } },
            { "track-hot", { "Nothing to hear on its own, but the plugins after it may clip.",
                             "Itsessään tätä ei kuule, mutta sen jälkeiset pluginit voivat klipata.",
                             "", "" } },
        };
        return t;
    }
}

void describeForEars (std::vector<Finding>& findings, Lang lang)
{
    const auto& t = table();
    for (auto& f : findings)
    {
        const auto it = t.find (f.id);
        if (it == t.end())
            continue;
        const auto& e = it->second;
        if (f.listenFor.isEmpty() && *e.listenEn != 0)
            f.listenFor = tr (lang, e.listenEn, e.listenFi);
        if (f.ignoreWhen.isEmpty() && *e.ignoreEn != 0)
            f.ignoreWhen = tr (lang, e.ignoreEn, e.ignoreFi);
    }
}
} // namespace wwma
