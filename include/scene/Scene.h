#ifndef TLE_SCOPE_SCENE_H
#define TLE_SCOPE_SCENE_H

#include "scene/BillboardHelper.h"
#include <raylib.h>
#include <string>
#include <map>

typedef std::multimap<std::string, Model> tModelNameMap;
typedef tModelNameMap::iterator tModelNameMapIt;

typedef std::multimap<std::string, Billboard*> tBillboardNameMap;
typedef tBillboardNameMap::iterator tBillboardNameMapIt;

class cScene
{
public:
	cScene();
	~cScene();

	void GenEarth();

	Model& GetModelByName(const std::string& argName);
	Billboard* GetBillboardByName(const std::string& argName);

	void AddBillboard(std::string argName, Billboard* argBillboard);

private:
	Texture2D earthTexture;
	Texture2D cloudTexture;

	tModelNameMap modelsbyName;
	tBillboardNameMap billboardsbyName;
};

#endif
