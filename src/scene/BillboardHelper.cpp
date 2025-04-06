#include "scene/BillboardHelper.h"
#include "TLEscope.h"

cBillboardHelper::cBillboardHelper()
{

}

cBillboardHelper::~cBillboardHelper()
{

}

void cBillboardHelper::CreateBillboard(std::string argName, Texture2D argText, Vector3 argPos, float argScale, Color argTint)
{
	Billboard* pBillboard = new cBillboard(BILLBOARD, argText, argPos, argScale, argTint);
	gpBase->GetScene()->AddBillboard(argName, pBillboard);
}

void cBillboardHelper::CreateBillboardRec(std::string argName, Texture2D argTex, Rectangle argSource, Vector3 argPos, Vector2 argSize, Color argTint)
{
	Billboard* pBillboardRec = new cBillboardRec(BILLBOARD_REC, argTex, argSource, argPos, argSize, argTint);
	gpBase->GetScene()->AddBillboard(argName, pBillboardRec);
}

void cBillboardHelper::CreateBillboardPro(std::string argName, Texture2D argTex, Rectangle argSource, Vector3 argPos, Vector3 argUp, Vector2 argSize, Vector2 argOrigin, float argRot, Color argTint)
{
	Billboard* pBillboardPro = new cBillboardPro(BILLBOARD_PRO, argTex, argSource, argPos, argUp, argSize, argOrigin, argRot, argTint);
	gpBase->GetScene()->AddBillboard(argName, pBillboardPro);
}