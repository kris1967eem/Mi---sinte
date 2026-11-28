#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "EffectChain.h"
#include "EffectParameterRegistry.h"

// ============================================================================
//  Presets de cadena de efectos
//
//  Guardan SOLO la configuracion de la cadena EFFECTS:
//    - el orden exacto de los 9 modulos,
//    - todos los parametros de cada efecto (ON/OFF incluido, modos, tiempos, algoritmos...).
//  No tocan nada mas del sintetizador: la lista de parametros sale de EffectParams (registro explicito),
//  de modo que osciladores, filtro, envolventes y LFOs quedan intactos al cargar un preset de cadena.
//
//  Los presets generales del sintetizador (el estado que guarda el DAW) ya incluyen los parametros
//  de efectos y el orden de la cadena: los dos sistemas conviven sin duplicar parametros.
//
//  Formato: un archivo XML por preset, "<Nombre>.misintefx", en
//      %APPDATA%/MiSinte/EffectChains/
//  Todo el acceso a disco y a parametros se hace en el hilo de mensajes (nunca en el de audio).
//
//  Validacion al cargar: se comprueba el tipo de archivo, la version y que el orden sea una
//  permutacion valida de los 9 modulos; los valores no numericos o de parametros desconocidos se
//  ignoran y los valores fuera de rango se limitan. Si el archivo no sirve, no se aplica nada.
//  Los parametros que el preset no incluya (p. ej. efectos anadidos despues) quedan en su valor
//  por defecto: cargar un preset SUSTITUYE por completo la cadena actual.
// ============================================================================
struct EffectChainSnapshot
{
    EffectOrder order = EffectOrderUtil::getDefault();
    std::vector<std::pair<juce::String, double>> values;     // id del parametro -> valor real (no normalizado)
};

class EffectChainPresets
{
public:
    static constexpr int kFormatVersion = 1;

    EffectChainPresets (juce::AudioProcessorValueTreeState& apvtsToUse, EffectChain& chainToUse)
        : apvts (apvtsToUse), chain (chainToUse), folder (getDefaultFolder())
    {
    }

    static juce::File getDefaultFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("MiSinte")
                   .getChildFile ("EffectChains");
    }

    juce::File getFolder() const { return folder; }

    // Nombre apto para archivo (sin caracteres prohibidos, maximo 64 letras). Vacio si no sirve.
    static juce::String makeLegalName (const juce::String& raw)
    {
        auto name = juce::File::createLegalFileName (raw.trim()).trim();
        name = name.trimCharactersAtEnd (". ");
        return name.substring (0, 64).trim();
    }

    // ---- Estado de la cadena <-> instantanea ------------------------------------------------

    EffectChainSnapshot capture() const
    {
        EffectChainSnapshot snapshot;
        snapshot.order = chain.getOrder();

        for (const auto& id : EffectParams::getAllEffectParameterIds())
            if (auto* p = apvts.getParameter (id))
                snapshot.values.emplace_back (id, (double) p->convertFrom0to1 (p->getValue()));

        return snapshot;
    }

    // Sustituye la cadena actual: primero todos los parametros de efectos a su valor por defecto, despues
    // los valores de la instantanea (los desconocidos se ignoran, los fuera de rango se limitan) y el orden.
    void apply (const EffectChainSnapshot& snapshot)
    {
        std::map<juce::String, double> wanted;
        for (const auto& entry : snapshot.values)
            if (std::isfinite (entry.second))
                wanted[entry.first] = entry.second;

        for (const auto& id : EffectParams::getAllEffectParameterIds())
        {
            auto* p = apvts.getParameter (id);
            if (p == nullptr)
                continue;

            float normalised = p->getDefaultValue();

            const auto it = wanted.find (id);
            if (it != wanted.end())
                normalised = p->convertTo0to1 ((float) it->second);

            p->beginChangeGesture();
            p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalised));
            p->endChangeGesture();
        }

        chain.setOrder (EffectOrderUtil::isValid (snapshot.order) ? snapshot.order : EffectOrderUtil::getDefault());
    }

    // Reset Chain: orden por defecto y todos los parametros de efectos en su valor por defecto
    void resetChain()
    {
        apply (EffectChainSnapshot());
    }

    // ---- XML --------------------------------------------------------------------------------

    static std::unique_ptr<juce::XmlElement> toXml (const EffectChainSnapshot& snapshot)
    {
        auto xml = std::make_unique<juce::XmlElement> ("MiSinteEffectChain");
        xml->setAttribute ("version", kFormatVersion);
        xml->createNewChildElement ("order")->setAttribute ("value", EffectOrderUtil::toText (snapshot.order));

        for (const auto& entry : snapshot.values)
        {
            auto* e = xml->createNewChildElement ("param");
            e->setAttribute ("id", entry.first);
            e->setAttribute ("value", entry.second);
        }

        return xml;
    }

    // Lee y valida un preset. Devuelve false (con el motivo en 'error') si no sirve; 'result' no se toca.
    static bool fromXml (const juce::XmlElement& xml, EffectChainSnapshot& result, juce::String& error)
    {
        if (! xml.hasTagName ("MiSinteEffectChain"))
        {
            error = "This is not a MiSinte effect chain file.";
            return false;
        }

        if (xml.getIntAttribute ("version", 0) < 1)
        {
            error = "Unknown file version.";
            return false;
        }

        const auto* orderXml = xml.getChildByName ("order");
        if (orderXml == nullptr)
        {
            error = "The chain order is missing.";
            return false;
        }

        EffectChainSnapshot snapshot;
        if (! EffectOrderUtil::fromText (orderXml->getStringAttribute ("value"), snapshot.order))
        {
            error = "The module order in the file is not valid.";
            return false;
        }

        for (auto* e : xml.getChildWithTagNameIterator ("param"))
        {
            const auto id = e->getStringAttribute ("id");
            if (id.isEmpty() || ! e->hasAttribute ("value"))
                continue;

            const double value = e->getDoubleAttribute ("value");
            if (std::isfinite (value))
                snapshot.values.emplace_back (id, value);
        }

        result = std::move (snapshot);
        return true;
    }

    // ---- Biblioteca de archivos ---------------------------------------------------------------

    juce::File fileFor (const juce::String& name) const
    {
        return folder.getChildFile (makeLegalName (name) + ".misintefx");
    }

    bool exists (const juce::String& name) const
    {
        return ! makeLegalName (name).isEmpty() && fileFor (name).existsAsFile();
    }

    juce::StringArray listPresets() const
    {
        juce::StringArray names;

        if (folder.isDirectory())
            for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.misintefx"))
                names.add (f.getFileNameWithoutExtension());

        names.sortNatural();
        return names;
    }

    // Guarda la cadena actual con ese nombre (sobrescribe si existe)
    bool save (const juce::String& name, juce::String& error) const
    {
        if (makeLegalName (name).isEmpty())
        {
            error = "Please type a valid name.";
            return false;
        }

        if (! folder.isDirectory() && ! folder.createDirectory().wasOk())
        {
            error = "The presets folder could not be created.";
            return false;
        }

        const auto xml = toXml (capture());
        if (! fileFor (name).replaceWithText (xml->toString()))
        {
            error = "The file could not be written.";
            return false;
        }

        return true;
    }

    // Carga un preset: valida el archivo y, solo si es correcto, sustituye la cadena actual
    bool load (const juce::String& name, juce::String& error)
    {
        const auto file = fileFor (name);
        if (! file.existsAsFile())
        {
            error = "The preset was not found.";
            return false;
        }

        const std::unique_ptr<juce::XmlElement> xml = juce::XmlDocument::parse (file);
        if (xml == nullptr)
        {
            error = "The file is damaged.";
            return false;
        }

        EffectChainSnapshot snapshot;
        if (! fromXml (*xml, snapshot, error))
            return false;

        apply (snapshot);
        return true;
    }

    bool rename (const juce::String& oldName, const juce::String& newName, juce::String& error) const
    {
        if (makeLegalName (newName).isEmpty())
        {
            error = "Please type a valid name.";
            return false;
        }

        const auto from = fileFor (oldName);
        const auto to = fileFor (newName);

        if (! from.existsAsFile())
        {
            error = "The preset was not found.";
            return false;
        }

        if (to == from)
            return true;

        if (to.exists())
        {
            error = "A preset with that name already exists.";
            return false;
        }

        if (! from.moveFileTo (to))
        {
            error = "The preset could not be renamed.";
            return false;
        }

        return true;
    }

    // Manda el preset a la papelera (si no se puede, lo borra)
    bool remove (const juce::String& name, juce::String& error) const
    {
        const auto file = fileFor (name);
        if (! file.existsAsFile())
        {
            error = "The preset was not found.";
            return false;
        }

        if (! file.moveToTrash() && ! file.deleteFile())
        {
            error = "The preset could not be deleted.";
            return false;
        }

        return true;
    }

private:
    juce::AudioProcessorValueTreeState& apvts;
    EffectChain& chain;
    juce::File folder;
};
