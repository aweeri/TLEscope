#ifndef TLE_SCOPE_SCENE_H
#define TLE_SCOPE_SCENE_H

#include <raylib.h>
#include <string>
#include <map>

typedef std::multimap<std::string, Model> tModelNameMap;
typedef tModelNameMap::iterator tModelNameMapIt;

class cScene
{
public:
	cScene();
	~cScene();

	void GenEarth();

	Model& GetModelByName(const std::string& name);

private:
	Texture2D earthTexture;
	Texture2D cloudTexture;

	tModelNameMap modelsbyName;
};

#endif
