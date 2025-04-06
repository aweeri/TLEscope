#ifndef TLE_SCOPE_BILLBOARD_H
#define TLE_SCOPE_BILLBOARD_H

#include <raylib.h>
#include <string>

typedef enum {
	BILLBOARD		= 0,
	BILLBOARD_REC	= 1,
	BILLBOARD_PRO	= 2
} BillboardType;

class cBillboardHelper
{
public:
	cBillboardHelper();
	~cBillboardHelper();

	void CreateBillboard(std::string argName, Texture2D argTex, Vector3 argPos, float argScale, Color argTint);
	void CreateBillboardRec(std::string argName, Texture2D argTex, Rectangle argSource, Vector3 argPos, Vector2 argSize, Color argTint);
	void CreateBillboardPro(std::string argName, Texture2D argTex, Rectangle argSource, Vector3 argPos, Vector3 argUp, Vector2 argSize, Vector2 argOrigin, float argRot, Color argTint);
};

class Billboard
{
public:
	virtual ~Billboard() {}

	virtual void SetSource(Rectangle argSource) {}
	virtual void SetUp(Vector3 argUp)			{}
	virtual void SetSize(Vector2 argSize)		{}
	virtual void SetOrigin(Vector2 argOrigin)	{}
	virtual void SetRotation(float argRot)		{}
	virtual void SetTexture(Texture2D argTex)	= 0;
	virtual void SetPosition(Vector3 argPos)	= 0;
	virtual void SetScale(float argScale)		= 0;
	virtual void SetTint(Color argTint)			= 0;

	virtual Rectangle GetSource() const			{ return { 0, 0, 0, 0 }; }
	virtual Vector3 GetUp() const				{ return { 0, 1, 0 }; }
	virtual Vector2 GetSize() const				{ return { 0, 0 }; }
	virtual Vector2 GetOrigin() const			{ return { 0, 0 }; }
	virtual float GetRotation() const			{ return 0.0f; }
	virtual int GetType() const					= 0;
	virtual Texture2D GetTexture() const		= 0;
	virtual Vector3 GetPosition() const			= 0;
	virtual float GetScale() const				= 0;
	virtual Color GetTint() const				= 0;
};

class cBillboard : public Billboard
{
public:
	cBillboard(int argType, Texture2D argTex, Vector3 argPos, float argScale, Color argTint)
		: type(argType), texture(argTex), position(argPos), scale(argScale), tint(argTint) {}

	~cBillboard()
	{
		UnloadTexture(texture);
	}

	void SetTexture(Texture2D argTex)	override { texture = argTex; }
	void SetPosition(Vector3 argPos)	override { position = argPos; }
	void SetScale(float argScale)		override { scale = argScale; }
	void SetTint(Color argTint)			override { tint = argTint; }

	int GetType()						const override { return type; }
	Texture2D GetTexture()				const override { return texture; }
	Vector3 GetPosition()				const override { return position; }
	float GetScale()					const override { return scale; }
	Color GetTint()						const override { return tint; }

private:
	int type;
	Texture2D texture;
	Vector3 position;
	float scale;
	Color tint;
};

class cBillboardRec : public Billboard
{
public:
	cBillboardRec(int argType, Texture2D argTex, Rectangle argSource, Vector3 argPos, Vector2 argSize, Color argTint)
		: type(argType), texture(argTex), source(argSource), position(argPos), size(argSize), tint(argTint) {}

	~cBillboardRec()
	{
		UnloadTexture(texture);
	}

	void SetTexture(Texture2D argTex)	override { texture = argTex; }
	void SetSource(Rectangle argSource)	override { source = argSource; }
	void SetPosition(Vector3 argPos)	override { position = argPos; }
	void SetSize(Vector2 argSize)		override { size = argSize; }
	void SetTint(Color argTint)			override { tint = argTint; }

	int GetType()						const override { return type; }
	Texture2D GetTexture()				const override { return texture; }
	Rectangle GetSource()				const override { return source; }
	Vector3 GetPosition()				const override { return position; }
	Vector2 GetSize()					const override { return size; }
	Color GetTint()						const override { return tint; }

	void SetScale(float argScale)		override {}
	float GetScale()					const override { return 1.0f; }

private:
	int type;
	Texture2D texture;
	Rectangle source;
	Vector3 position;
	Vector2 size;
	Color tint;
};

class cBillboardPro : public Billboard
{
public:
	cBillboardPro(int argType, Texture2D argTex, Rectangle argSource, Vector3 argPos, Vector3 argUp, Vector2 argSize, Vector2 argOrigin, float argRot, Color argTint)
		: type(argType), texture(argTex), source(argSource), position(argPos), up(argUp), size(argSize), origin(argOrigin), rotation(argRot) , tint(argTint) {}

	~cBillboardPro()
	{
		UnloadTexture(texture);
	}

	void SetTexture(Texture2D argTex)	override { texture = argTex; }
	void SetSource(Rectangle argSource) override { source = argSource; }
	void SetPosition(Vector3 argPos)	override { position = argPos; }
	void SetUp(Vector3 argUp)			override { up = argUp; }
	void SetSize(Vector2 argSize)		override { size = argSize; }
	void SetOrigin(Vector2 argOrigin)	override { origin = argOrigin; }
	void SetRotation(float argRot)		override { rotation = argRot; }
	void SetTint(Color argTint)			override { tint = argTint; }

	int GetType()						const override { return type; }
	Texture2D GetTexture()				const override { return texture; }
	Rectangle GetSource()				const override { return source; }
	Vector3 GetPosition()				const override { return position; }
	Vector3 GetUp()						const override { return up; }
	Vector2 GetSize()					const override { return size; }
	Vector2 GetOrigin()					const override { return origin; }
	float GetRotation()					const override { return rotation; }
	Color GetTint()						const override { return tint; }

	void SetScale(float argScale)		override {}
	float GetScale()					const override { return 1.0f; }

private:
	int type;
	Texture2D texture;
	Rectangle source;
	Vector3 position;
	Vector3 up;
	Vector2 size;
	Vector2 origin;
	float rotation;
	Color tint;
};

#endif