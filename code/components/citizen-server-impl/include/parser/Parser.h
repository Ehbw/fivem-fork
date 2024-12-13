#pragma once

#include "StdInc.h"
#include <tinyxml2.h>

#include <ScriptWarnings.h>
#include <ClientRegistry.h>
#include <ResourceManager.h>
#include <VFSManager.h>

#include <GameServer.h>
#include <CrossBuildRuntime.h>

namespace fx
{
	class BaseParser : public fwRefCountable
	{
	public:
		virtual bool LoadFile(fwRefContainer<fx::Resource> resource, std::string_view file, bool replaceExistingData) = 0;
		virtual bool LoadFile(std::wstring_view file) = 0;
	protected:
		virtual bool ParseFile(tinyxml2::XMLDocument & document, bool replaceExistingData) = 0;
	};

	template<typename T>
	class DataFileParser : public BaseParser
	{
	protected:
		T m_data;
	public:
		// Loads the specified file from the resources root path
		bool LoadFile(fwRefContainer<fx::Resource> resource, std::string_view file, bool replaceExistingData = true)
		{
			const std::string& rootPath = resource->GetPath();

			if (rootPath.empty())
			{
				return false;
			}

			fwRefContainer<vfs::Stream> stream = vfs::OpenRead(rootPath + "/" + std::string{ file });

			if (!stream.GetRef())
			{
				trace("Unable to locate file at @%s/%s\n", resource->GetName(), file);
				return false;
			}

			auto data = stream->ReadToEnd();
			tinyxml2::XMLDocument document;
			tinyxml2::XMLError error = document.Parse(reinterpret_cast<const char*>(data.data()), data.size());

			if (error == tinyxml2::XML_SUCCESS)
			{
				return ParseFile(document, replaceExistingData);
			}

			trace("Unable to parse xml document at @%s/%s, error %i\n", resource->GetName(), file, error);

			return false;
		}

		// Loads the specified file from citizen/data
		bool LoadFile(std::wstring_view file) override
		{
			const std::wstring path = MakeRelativeCitPath(fmt::sprintf(L"citizen/data/%s", file));

			fwRefContainer<vfs::Stream> stream = vfs::OpenRead(ToNarrow(path));

			if (!stream.GetRef())
			{
				fx::scripting::Warningf("", "Unable to locate file at citizen/data/%s\n", ToNarrow(file));
				return false;
			}

			auto data = stream->ReadToEnd();
			tinyxml2::XMLDocument document;
			tinyxml2::XMLError error = document.Parse(reinterpret_cast<const char*>(data.data()), data.size());

			if (error == tinyxml2::XML_SUCCESS)
			{
				return ParseFile(document, true);
			}

			return true;
		}

		T& GetData()
		{
			return m_data;
		};
	};

	struct TrainTracks
	{
		const char* m_trainConfig;
	};

	// FXServers need no information about the train track aside from how many registered tracks exist.
	class TrainTrackParser : public DataFileParser<int>
	{
	public:
		TrainTrackParser()
		{
			m_data = 0;
			this->LoadFile(L"traintracks.xml");
		}
	protected:
		bool ParseFile(tinyxml2::XMLDocument& document, bool replaceExistingData)
		{
			if (replaceExistingData)
			{
				// Registering a TRAINCONFIGS_FILE data_file overrides existing entries.
				m_data = 0;
			}

			if (m_data >= 27)
			{
				fx::scripting::Warningf("parser", "Too many train tracks loaded, Only 27 tracks can be registered at a time.\n");
				return false;
			}

			tinyxml2::XMLElement* tracks = document.FirstChildElement("train_tracks");
			if (!tracks)
			{
				fx::scripting::Warningf("parser", "Missing train_tracks xml definition.\n");
				return false;
			}

			std::vector<TrainTracks> trainTracks;
			tinyxml2::XMLElement* track = tracks->FirstChildElement("train_track");

			if (!track)
			{
				fx::scripting::Warningf("parser", "Missing or malformed train_track xml definition\n");
				return false;
			}

			while (track)
			{
				m_data++;
				track = track->NextSiblingElement("train_track");
			}

			return true;
		}
	};
	
	struct CCarriageData
	{
		uint32_t m_modelHash;
		int m_repeatCount;
	};

	struct CTrainConfig
	{
		const char* m_name;
		float m_carriageGap;
		std::vector<CCarriageData> m_carriages;
	};

	// We need knowledge of the carriageGap and carriages in each train config.
	class TrainConfigParser : public DataFileParser<std::vector<CTrainConfig>>
	{
	public:
		TrainConfigParser()
		{
			this->LoadFile(L"trains.xml");
		}
	protected:
		bool ParseFile(tinyxml2::XMLDocument& document, bool replaceExistingData)
		{
			if (replaceExistingData)
			{
				m_data.clear();
			}

			tinyxml2::XMLElement* configs = document.FirstChildElement("train_configs");

			if (!configs)
			{
				fx::scripting::Warningf("parser", "Missing or malformed 'train_configs' xml definition\n");
				return false;
			}

			std::vector<CTrainConfig> trainConfigs;
			tinyxml2::XMLElement* config = configs->FirstChildElement("train_config");

			if (!config)
			{
				return false;
			}

			while (config)
			{
				CTrainConfig trainConfig;
				trainConfig.m_name = config->Attribute("name");
				trainConfig.m_carriageGap = config->FloatAttribute("carriage_gap");

				int introducedGameBuild = config->IntAttribute("gameBuild", 0);

				// Skip this entry and move to the next
				if (introducedGameBuild > fx::GetEnforcedGameBuildNumber())
				{
					config = config->NextSiblingElement("train_config");
					continue;
				}

				tinyxml2::XMLElement* carriage = config->FirstChildElement("carriage");

				if (!carriage)
				{
					fx::scripting::Warningf("parser", "Invalid train_config structure. Requires atleast one 'carriage' child element\n");
					return false;
				}
				
				while (carriage)
				{
					CCarriageData carriageData{};
					carriageData.m_modelHash = HashRageString(carriage->FirstAttribute()->Value()); //carriage->Attribute("model_name", "invalid");
					carriageData.m_repeatCount = carriage->IntAttribute("repeat_count", 0);

					trainConfig.m_carriages.push_back(carriageData);
					carriage = carriage->NextSiblingElement("carriage");
				}
				
				trainConfigs.push_back(trainConfig);
				config = config->NextSiblingElement("train_config");
			}

			m_data = std::move(trainConfigs);
			return true;
		}
	};
}

DECLARE_INSTANCE_TYPE(fx::TrainConfigParser);
DECLARE_INSTANCE_TYPE(fx::TrainTrackParser);
