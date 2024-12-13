#include "StdInc.h"

#include <ResourceManager.h>
#include <ResourceEventComponent.h>
#include <ResourceMetaDataComponent.h>
#include <ResourceCallbackComponent.h>

#include <ServerInstanceBase.h>
#include <ServerInstanceBaseRef.h>

#include <fxScripting.h>
#include <ScriptEngine.h>

#include "parser/Parser.h"

static std::vector<std::string> g_supportedDataFiles
{ 
	"TRAINCONFIGS_FILE", 
	"TRAINTRACK_FILE"
};

static fwRefContainer<fx::BaseParser> GetDataParser(fx::ServerInstanceBase* instance, std::string_view name)
{
	const int hash = HashRageString(name);

	switch (hash)
	{
	case HashRageString("TRAINCONFIGS_FILE"):
		return instance->GetComponent<fx::TrainConfigParser>();
	case HashRageString("TRAINTRACK_FILE"):
		return instance->GetComponent<fx::TrainTrackParser>();
	default:
		return nullptr;
	}
}

static InitFunction initFunction([]()
{
	fx::ServerInstanceBase::OnServerCreate.Connect([](fx::ServerInstanceBase* instance)
	{
		using fx::GameName;
		static auto gamename = std::make_shared<ConVar<GameName>>("gamename", ConVar_ServerInfo, GameName::GTA5);

		// Only support parsing for GTA5
		if (gamename->GetValue() != GameName::GTA5)
		{
			return;
		}

		// Create instances of the parsers and add them as components
		for (const auto& dataFile : g_supportedDataFiles)
		{
			const int hash = HashRageString(dataFile);

			switch (hash)
			{
			case HashRageString("TRAINCONFIGS_FILE"): 
			{
				fwRefContainer<fx::TrainConfigParser> trainConfigParser = new fx::TrainConfigParser();
				instance->SetComponent<fx::TrainConfigParser>(trainConfigParser);
				break;
			}
			case HashRageString("TRAINTRACK_FILE"):
			{
				fwRefContainer<fx::TrainTrackParser> trainTrackParser = new fx::TrainTrackParser();
				instance->SetComponent<fx::TrainTrackParser>(trainTrackParser);
				break;
			}
			}
		}

		fx::Resource::OnInitializeInstance.Connect([instance](fx::Resource* resource)
		{
			// Check through all data_file entries in the resources manifest for any references to supported parsers.
			resource->OnStart.Connect([instance, resource]()
			{
				auto metaData = resource->GetComponent<fx::ResourceMetaDataComponent>();
				auto dataFiles = metaData->GetEntries("data_file");

				// No data_file entries in resource manifest.
				if (dataFiles.begin() == dataFiles.end())
				{
					return;
				}

				auto dataName = metaData->GetEntries("data_file_extra");

				for (auto it1 = dataFiles.begin(), end1 = dataFiles.end(), it2 = dataName.begin(), end2 = dataName.end(); it1 != end1 && it2 != end2; ++it1, ++it2)
				{
					const std::string& type = it1->second;
					const std::string& name = it2->second;

					rapidjson::Document document;
					document.Parse(name.c_str(), name.length());

					if (!document.HasParseError() && document.IsString())
					{
						std::string key = document.GetString();

						// Is this data_file type supported?
						if (std::find(g_supportedDataFiles.begin(), g_supportedDataFiles.end(), type) != g_supportedDataFiles.end())
						{	
							trace("Found supported data_file %s for resource '%s'\n", type, resource->GetName());

							// Get the parser instance from the server instance
							fwRefContainer<fx::BaseParser> parser = GetDataParser(instance, type);

							if (parser.GetRef())
							{
							   parser->LoadFile(resource, key, true);
							}
						}
					}
				}
			}, 1000);
		});
	}, -1000);
});
