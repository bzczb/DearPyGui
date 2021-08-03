#include "mvStaticTexture.h"
#include "mvLog.h"
#include "mvItemRegistry.h"
#include "mvPythonExceptions.h"
#include "mvUtilities.h"
#include <fstream>

namespace Marvel {

	void mvStaticTexture::InsertParser(std::map<std::string, mvPythonParser>* parsers)
	{

		mvPythonParser parser(mvPyDataType::UUID, "Undocumented function", { "Textures", "Widgets" });
		mvAppItem::AddCommonArgs(parser, (CommonParserArgs)(
			MV_PARSER_ARG_ID)
		);

		parser.addArg<mvPyDataType::Integer>("width");
		parser.addArg<mvPyDataType::Integer>("height");
		parser.addArg<mvPyDataType::FloatList>("default_value");

		parser.addArg<mvPyDataType::UUID>("parent", mvArgType::KEYWORD_ARG, "internal_dpg.mvReservedUUID_2", "Parent to add this item to. (runtime adding)");
		parser.finalize();

		parsers->insert({ s_command, parser });
	}

	mvStaticTexture::mvStaticTexture(mvUUID uuid)
		:
		mvAppItem(uuid)
	{

	}

	mvStaticTexture::~mvStaticTexture()
	{
		if (_uuid == MV_ATLAS_UUID)
			return;
		//UnloadTexture(_name);
		FreeTexture(_texture);
	}


	bool mvStaticTexture::isParentCompatible(mvAppItemType type)
	{
		if (type == mvAppItemType::mvStagingContainer) return true;
		if (type == mvAppItemType::mvTextureRegistry) return true;

		mvThrowPythonError(mvErrorCode::mvIncompatibleParent, s_command,
			"Incompatible parent. Acceptable parents include: mvTextureRegistry, mvStagingContainer.", this);

		MV_ITEM_REGISTRY_ERROR("Drawing item parent must be a drawing.");
		assert(false);
		return false;
	}

	void mvStaticTexture::draw(ImDrawList* drawlist, float x, float y)
	{
		if (!_dirty)
			return;

		if (!_state.isOk())
			return;

		if (_uuid == MV_ATLAS_UUID)
		{
			_texture = ImGui::GetIO().Fonts->TexID;
			_width = ImGui::GetIO().Fonts->TexWidth;
			_height = ImGui::GetIO().Fonts->TexHeight;
		}
		else
			_texture = LoadTextureFromArray(_width, _height, _value->data());

		if (_texture == nullptr)
		{
			_state.setOk(false);
			mvThrowPythonError(mvErrorCode::mvItemNotFound, "add_static_texture",
				"Texture data can not be found.", this);
		}


		_dirty = false;
	}

	void mvStaticTexture::handleSpecificRequiredArgs(PyObject* dict)
	{
		if (!mvApp::GetApp()->getParsers()[s_command].verifyRequiredArguments(dict))
			return;

		for (int i = 0; i < PyTuple_Size(dict); i++)
		{
			PyObject* item = PyTuple_GetItem(dict, i);
			switch (i)
			{
			case 0:
				_width = ToInt(item);
				break;

			case 1:
				_height = ToInt(item);
				break;

			case 2:
				*_value = ToFloatVect(item);
				break;

			default:
				break;
			}
		}
	}

	PyObject* mvStaticTexture::getPyValue()
	{
		return ToPyList(*_value);
	}

	void mvStaticTexture::setPyValue(PyObject* value)
	{
		*_value = ToFloatVect(value);
	}

	void mvStaticTexture::setDataSource(mvUUID dataSource)
	{
		if (dataSource == _source) return;
		_source = dataSource;

		mvAppItem* item = mvApp::GetApp()->getItemRegistry().getItem(dataSource);
		if (!item)
		{
			mvThrowPythonError(mvErrorCode::mvSourceNotFound, "set_value",
				"Source item not found: " + std::to_string(dataSource), this);
			return;
		}
		if (item->getValueType() != getValueType())
		{
			mvThrowPythonError(mvErrorCode::mvSourceNotCompatible, "set_value",
				"Values types do not match: " + std::to_string(dataSource), this);
			return;
		}
		_value = std::get<std::shared_ptr<std::vector<float>>>(item->getValue());
	}

}