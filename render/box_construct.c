/*
 * Copyright 2005 James Bursa <bursa@users.sourceforge.net>
 * Copyright 2003 Phil Mellor <monkeyson@users.sourceforge.net>
 * Copyright 2005 John M Bell <jmb202@ecs.soton.ac.uk>
 * Copyright 2006 Richard Wilson <info@tinct.net>
 * Copyright 2008 Michael Drake <tlsa@netsurf-browser.org>
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/** \file
 * Conversion of XML tree to box tree (implementation).
 */

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <libxml/HTMLparser.h>
#include <libxml/parserInternals.h>
#include "utils/config.h"
#include "content/content_protected.h"
#include "css/css.h"
#include "css/utils.h"
#include "css/select.h"
#include "desktop/browser.h"
#include "desktop/options.h"
#include "render/box.h"
#include "render/form.h"
#include "render/html_internal.h"
#include "desktop/gui.h"
#include "utils/locale.h"
#include "utils/log.h"
#include "utils/messages.h"
#include "utils/talloc.h"
#include "utils/url.h"
#include "utils/utils.h"


static const content_type image_types = CONTENT_IMAGE;

/* the strings are not important, since we just compare the pointers */
const char *TARGET_SELF = "_self";
const char *TARGET_PARENT = "_parent";
const char *TARGET_TOP = "_top";
const char *TARGET_BLANK = "_blank";

static bool convert_xml_to_box(xmlNode *n, html_content *content,
		const css_computed_style *parent_style,
		struct box *parent, struct box **inline_container,
		char *href, const char *target, char *title);
bool box_construct_element(xmlNode *n, html_content *content,
		const css_computed_style *parent_style,
		struct box *parent, struct box **inline_container,
		char *href, const char *target, char *title);
void box_construct_generate(xmlNode *n, html_content *content,
		struct box *box, const css_computed_style *style);
bool box_construct_text(xmlNode *n, html_content *content,
		const css_computed_style *parent_style,
		struct box *parent, struct box **inline_container,
		char *href, const char *target, char *title);
static css_select_results * box_get_style(html_content *c,
		const css_computed_style *parent_style, xmlNode *n);
static void box_text_transform(char *s, unsigned int len,
		enum css_text_transform_e tt);
#define BOX_SPECIAL_PARAMS xmlNode *n, html_content *content, \
		struct box *box, bool *convert_children
static bool box_a(BOX_SPECIAL_PARAMS);
static bool box_body(BOX_SPECIAL_PARAMS);
static bool box_br(BOX_SPECIAL_PARAMS);
static bool box_image(BOX_SPECIAL_PARAMS);
static bool box_textarea(BOX_SPECIAL_PARAMS);
static bool box_select(BOX_SPECIAL_PARAMS);
static bool box_input(BOX_SPECIAL_PARAMS);
static bool box_input_text(BOX_SPECIAL_PARAMS, bool password);
static bool box_button(BOX_SPECIAL_PARAMS);
static bool box_frameset(BOX_SPECIAL_PARAMS);
static bool box_create_frameset(struct content_html_frames *f, xmlNode *n,
		html_content *content);
static bool box_select_add_option(struct form_control *control, xmlNode *n);
static bool box_object(BOX_SPECIAL_PARAMS);
static bool box_embed(BOX_SPECIAL_PARAMS);
static bool box_pre(BOX_SPECIAL_PARAMS);
static bool box_iframe(BOX_SPECIAL_PARAMS);
static bool box_get_attribute(xmlNode *n, const char *attribute,
		void *context, char **value);
static struct frame_dimension *box_parse_multi_lengths(const char *s,
		unsigned int *count);

/* element_table must be sorted by name */
struct element_entry {
	char name[10];	 /* element type */
	bool (*convert)(BOX_SPECIAL_PARAMS);
};
static const struct element_entry element_table[] = {
	{"a", box_a},
	{"body", box_body},
	{"br", box_br},
	{"button", box_button},
	{"embed", box_embed},
	{"frameset", box_frameset},
	{"iframe", box_iframe},
	{"image", box_image},
	{"img", box_image},
	{"input", box_input},
	{"object", box_object},
	{"pre", box_pre},
	{"select", box_select},
	{"textarea", box_textarea}
};
#define ELEMENT_TABLE_COUNT (sizeof(element_table) / sizeof(element_table[0]))

/**
 * Construct a box tree from an xml tree and stylesheets.
 *
 * \param  n  xml tree
 * \param  c  content of type CONTENT_HTML to construct box tree in
 * \return  true on success, false on memory exhaustion
 */

bool xml_to_box(xmlNode *n, html_content *c)
{
	struct box root;
	struct box *inline_container = NULL;

	root.type = BOX_BLOCK;
	root.style = NULL;
	root.next = NULL;
	root.prev = NULL;
	root.children = NULL;
	root.last = NULL;
	root.parent = NULL;
	root.float_children = NULL;
	root.next_float = NULL;

	/* The root box's style */
	if (!convert_xml_to_box(n, c, NULL, &root,
			&inline_container, 0, 0, 0))
		return false;

	if (!box_normalise_block(&root, c))
		return false;

	c->layout = root.children;
	c->layout->parent = NULL;

	return true;
}


/* mapping from CSS display to box type
 * this table must be in sync with libcss' css_display enum */
static const box_type box_map[] = {
	0, /*CSS_DISPLAY_INHERIT,*/
	BOX_INLINE, /*CSS_DISPLAY_INLINE,*/
	BOX_BLOCK, /*CSS_DISPLAY_BLOCK,*/
	BOX_BLOCK, /*CSS_DISPLAY_LIST_ITEM,*/
	BOX_INLINE, /*CSS_DISPLAY_RUN_IN,*/
	BOX_INLINE_BLOCK, /*CSS_DISPLAY_INLINE_BLOCK,*/
	BOX_TABLE, /*CSS_DISPLAY_TABLE,*/
	BOX_TABLE, /*CSS_DISPLAY_INLINE_TABLE,*/
	BOX_TABLE_ROW_GROUP, /*CSS_DISPLAY_TABLE_ROW_GROUP,*/
	BOX_TABLE_ROW_GROUP, /*CSS_DISPLAY_TABLE_HEADER_GROUP,*/
	BOX_TABLE_ROW_GROUP, /*CSS_DISPLAY_TABLE_FOOTER_GROUP,*/
	BOX_TABLE_ROW, /*CSS_DISPLAY_TABLE_ROW,*/
	BOX_NONE, /*CSS_DISPLAY_TABLE_COLUMN_GROUP,*/
	BOX_NONE, /*CSS_DISPLAY_TABLE_COLUMN,*/
	BOX_TABLE_CELL, /*CSS_DISPLAY_TABLE_CELL,*/
	BOX_INLINE, /*CSS_DISPLAY_TABLE_CAPTION,*/
	BOX_NONE /*CSS_DISPLAY_NONE*/
};


/**
 * Recursively construct a box tree from an xml tree and stylesheets.
 *
 * \param  n		 fragment of xml tree
 * \param  content	 content of type CONTENT_HTML that is being processed
 * \param  parent_style  style at this point in xml tree, or NULL for root box
 * \param  parent	 parent in box tree
 * \param  inline_container  current inline container box, or 0, updated to
 *			 new current inline container on exit
 * \param  href		 current link URL, or 0 if not in a link
 * \param  target	 current link target, or 0 if none
 * \param  title	 current title, or 0 if none
 * \return  true on success, false on memory exhaustion
 */

bool convert_xml_to_box(xmlNode *n, html_content *content,
		const css_computed_style *parent_style,
		struct box *parent, struct box **inline_container,
		char *href, const char *target, char *title)
{
	switch (n->type) {
	case XML_ELEMENT_NODE:
		return box_construct_element(n, content, parent_style, parent,
				inline_container, href, target, title);
	case XML_TEXT_NODE:
		return box_construct_text(n, content, parent_style, parent,
				inline_container, href, target, title);
	default:
		/* not an element or text node: ignore it (eg. comment) */
		return true;
	}
}


/**
 * Construct the box tree for an XML element.
 *
 * \param  n		 XML node of type XML_ELEMENT_NODE
 * \param  content	 content of type CONTENT_HTML that is being processed
 * \param  parent_style  style at this point in xml tree, or NULL for root node
 * \param  parent	 parent in box tree
 * \param  inline_container  current inline container box, or 0, updated to
 *			 new current inline container on exit
 * \param  href		 current link URL, or 0 if not in a link
 * \param  target	 current link target, or 0 if none
 * \param  title	 current title, or 0 if none
 * \return  true on success, false on memory exhaustion
 */

bool box_construct_element(xmlNode *n, html_content *content,
		const css_computed_style *parent_style,
		struct box *parent, struct box **inline_container,
		char *href, const char *target, char *title)
{
	bool convert_children = true;
	char *id = 0;
	char *s;
	struct box *box = 0;
	struct box *inline_container_c;
	struct box *inline_end;
	css_select_results *styles = NULL;
	struct element_entry *element;
	xmlChar *title0;
	xmlNode *c;
	lwc_string *bgimage_uri;

	assert(n);
	assert(n->type == XML_ELEMENT_NODE);
	assert(parent);
	assert(inline_container);

	gui_multitask();

	/* In case the parent is a pre block, we clear the
	 * PRE_STRIP flag since it is not used if we
	 * follow the pre with a tag
	 */
	parent->flags &= ~PRE_STRIP;

	styles = box_get_style(content, parent_style, n);
	if (!styles)
		return false;

	/* extract title attribute, if present */
	if ((title0 = xmlGetProp(n, (const xmlChar *) "title"))) {
		char *title1 = squash_whitespace((char *) title0);

		xmlFree(title0);

		if (!title1)
			return false;

		title = talloc_strdup(content, title1);

		free(title1);

		if (!title)
			return false;
	}

	/* extract id attribute, if present */
	if (!box_get_attribute(n, "id", content, &id))
		return false;

	/* create box for this element */
	box = box_create(styles, styles->styles[CSS_PSEUDO_ELEMENT_NONE], false,
			href, target, title, id, content);
	if (!box)
		return false;
	/* set box type from computed display */
	if ((css_computed_position(box->style) == CSS_POSITION_ABSOLUTE ||
			css_computed_position(box->style) ==
					CSS_POSITION_FIXED) &&
			(css_computed_display_static(box->style) == 
					CSS_DISPLAY_INLINE ||
			 css_computed_display_static(box->style) == 
					CSS_DISPLAY_INLINE_BLOCK ||
			 css_computed_display_static(box->style) == 
					CSS_DISPLAY_INLINE_TABLE)) {
		/* Special case for absolute positioning: make absolute inlines
		 * into inline block so that the boxes are constructed in an 
		 * inline container as if they were not absolutely positioned. 
		 * Layout expects and handles this. */
		box->type = box_map[CSS_DISPLAY_INLINE_BLOCK];
	} else {
		/* Normal mapping */
		box->type = box_map[css_computed_display(box->style, 
				n->parent == NULL)];
	}

	/* handle the :before pseudo element */

	/* TODO: Replace with true implementation.
	 * Currently we only implement enough of this to support the
	 * 'clearfix' hack, which is in widespread use and the layout
	 * of many sites depend on. As such, only bother if box is a
	 * block for now. */
	if (box->type == BOX_BLOCK) {
		box_construct_generate(n, content, box,
				box->styles->styles[CSS_PSEUDO_ELEMENT_BEFORE]);
	}

	/* special elements */
	element = bsearch((const char *) n->name, element_table,
			ELEMENT_TABLE_COUNT, sizeof(element_table[0]),
			(int (*)(const void *, const void *)) strcmp);
	if (element) {
		/* a special convert function exists for this element */
		if (!element->convert(n, content, box, &convert_children))
			return false;

		href = box->href;
		target = box->target;
	}

	if (box->type == BOX_NONE || css_computed_display(box->style, 
			n->parent == NULL) == CSS_DISPLAY_NONE) {
		/* Free style and invalidate box's style pointer */
		css_select_results_destroy(styles);
		box->styles = NULL;
		box->style = NULL;

		/* If this box has an associated gadget, invalidate the
		 * gadget's box pointer and our pointer to the gadget. */
		if (box->gadget) {
			box->gadget->box = NULL;
			box->gadget = NULL;
		}

		/* We can't do this, as it will destroy any gadget
		 * associated with the box, thus making any form usage
		 * access freed memory. The box is in the talloc context,
		 * anyway, so will get cleaned up with the content. */
		/* box_free_box(box); */
		return true;
	}

	if (!*inline_container &&
			(box->type == BOX_INLINE ||
			box->type == BOX_BR ||
			box->type == BOX_INLINE_BLOCK ||
			css_computed_float(box->style) == CSS_FLOAT_LEFT ||
			css_computed_float(box->style) == CSS_FLOAT_RIGHT)) {
		/* this is the first inline in a block: make a container */
		*inline_container = box_create(NULL, 0, false, 0, 0, 0, 0,
				content);
		if (!*inline_container)
			return false;

		(*inline_container)->type = BOX_INLINE_CONTAINER;

		box_add_child(parent, *inline_container);
	}

	if (box->type == BOX_INLINE || box->type == BOX_BR) {
		/* inline box: add to tree and recurse */
		box_add_child(*inline_container, box);

		if (convert_children && n->children) {
			for (c = n->children; c; c = c->next)
				if (!convert_xml_to_box(c, content, box->style,
						parent, inline_container,
						href, target, title))
					return false;

			inline_end = box_create(NULL, box->style, false, href,
					target, title, id, content);
			if (!inline_end)
				return false;

			inline_end->type = BOX_INLINE_END;

			if (*inline_container)
				box_add_child(*inline_container, inline_end);
			else
				box_add_child(box->parent, inline_end);

			box->inline_end = inline_end;
			inline_end->inline_end = box;
		}
	} else if (box->type == BOX_INLINE_BLOCK) {
		/* inline block box: add to tree and recurse */
		box_add_child(*inline_container, box);

		inline_container_c = 0;

		for (c = n->children; convert_children && c; c = c->next)
			if (!convert_xml_to_box(c, content, box->style, box,
					&inline_container_c,
					href, target, title))
				return false;
	} else {
		/* list item: compute marker, then treat as non-inline box */
		if (css_computed_display(box->style, n->parent == NULL) == 
				CSS_DISPLAY_LIST_ITEM) {
			lwc_string *image_uri;
			struct box *marker;

			marker = box_create(NULL, box->style, false, 0, 0,
					title, 0, content);
			if (!marker)
				return false;

			marker->type = BOX_BLOCK;

			/** \todo marker content (list-style-type) */
			switch (css_computed_list_style_type(box->style)) {
			case CSS_LIST_STYLE_TYPE_DISC:
				/* 2022 BULLET */
				marker->text = (char *) "\342\200\242";
				marker->length = 3;
				break;
			case CSS_LIST_STYLE_TYPE_CIRCLE:
				/* 25CB WHITE CIRCLE */
				marker->text = (char *) "\342\227\213";
				marker->length = 3;
				break;
			case CSS_LIST_STYLE_TYPE_SQUARE:
				/* 25AA BLACK SMALL SQUARE */
				marker->text = (char *) "\342\226\252";
				marker->length = 3;
				break;
			case CSS_LIST_STYLE_TYPE_DECIMAL:
			case CSS_LIST_STYLE_TYPE_LOWER_ALPHA:
			case CSS_LIST_STYLE_TYPE_LOWER_ROMAN:
			case CSS_LIST_STYLE_TYPE_UPPER_ALPHA:
			case CSS_LIST_STYLE_TYPE_UPPER_ROMAN:
			default:
				if (parent->last) {
					struct box *last = parent->last;

					/* Drill down into last child of parent
					 * to find the list marker (if any)
					 *
					 * Floated list boxes end up as:
					 *
					 * parent
					 *   BOX_INLINE_CONTAINER
					 *     BOX_FLOAT_{LEFT,RIGHT}
					 *       BOX_BLOCK <-- list box
					 *        ...
					 */
					while (last != NULL) {
						if (last->list_marker != NULL)
							break;

						last = last->last;
					}

					if (last && last->list_marker) {
						marker->rows = last->
							list_marker->rows + 1;
					}
				}

				marker->text = talloc_array(content, char, 20);
				if (!marker->text)
					return false;

				snprintf(marker->text, 20, "%u.", marker->rows);
				marker->length = strlen(marker->text);
				break;
			case CSS_LIST_STYLE_TYPE_NONE:
				marker->text = 0;
				marker->length = 0;
				break;
			}

			if (css_computed_list_style_image(box->style,
					&image_uri) ==
					CSS_LIST_STYLE_IMAGE_URI && 
					image_uri != NULL) {
				if (!html_fetch_object(content,
						lwc_string_data(image_uri),
						marker, image_types,
						content->base.available_width,
						1000, false))
					return false;
			}

			box->list_marker = marker;
			marker->parent = box;
		}

		/* float: insert a float box between the parent and
		 * current node. Note: new parent will be the float */
		if (css_computed_float(box->style) == CSS_FLOAT_LEFT ||
				css_computed_float(box->style) ==
				CSS_FLOAT_RIGHT) {
			parent = box_create(NULL, 0, false, href, target, title,
					0, content);
			if (!parent)
				return false;

			if (css_computed_float(box->style) == CSS_FLOAT_LEFT)
				parent->type = BOX_FLOAT_LEFT;
			else
				parent->type = BOX_FLOAT_RIGHT;

			box_add_child(*inline_container, parent);
		}

		/* non-inline box: add to tree and recurse */
		box_add_child(parent, box);

		inline_container_c = 0;

		for (c = n->children; convert_children && c; c = c->next)
			if (!convert_xml_to_box(c, content, box->style, box,
					&inline_container_c,
					href, target, title))
				return false;

		if (css_computed_float(box->style) == CSS_FLOAT_NONE)
			/* new inline container unless this is a float */
			*inline_container = 0;
	}

	/* misc. attributes that can't be handled in box_get_style() */
	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "colspan"))) {
	  	if (isdigit(s[0])) {
			box->columns = strtol(s, NULL, 10);
		}
		xmlFree(s);
	}

	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "rowspan"))) {
	  	if (isdigit(s[0])) {
			box->rows = strtol(s, NULL, 10);
		}
		xmlFree(s);
	}

	/* fetch any background image for this box */
	if (css_computed_background_image(box->style, &bgimage_uri) ==
				CSS_BACKGROUND_IMAGE_IMAGE &&
				bgimage_uri != NULL) {
		if (!html_fetch_object(content,
				lwc_string_data(bgimage_uri),
				box, image_types, content->base.available_width,
				1000, true))
			return false;
	}

	/* handle the :after pseudo element */

	/* TODO: Replace with true implementation.
	 * Currently we only implement enough of this to support the
	 * 'clearfix' hack, which is in widespread use and the layout
	 * of many sites depend on. As such, only bother if box is a
	 * block for now. */
	if (box->type == BOX_BLOCK) {
		box_construct_generate(n, content, box,
				box->styles->styles[CSS_PSEUDO_ELEMENT_AFTER]);
	}

	return true;
}

/**
 * Construct the box required for a generated element.
 *
 * \param  n		XML node of type XML_ELEMENT_NODE
 * \param  content	content of type CONTENT_HTML that is being processed
 * \param  box		box which may have generated content
 * \param  style	complete computed style for pseudo element
 *
 * TODO:
 * This is currently incomplete. It just does enough to support the clearfix
 * hack. ( http://www.positioniseverything.net/easyclearing.html )
 *
 * To determine if an element has a pseudo element, we select for it and test to
 * see if the returned style's content property is set to normal.
 *
 * We don't actually support generated content yet.
 */

void box_construct_generate(xmlNode *n, html_content *content,
		struct box *box, const css_computed_style *style)
{
	struct box *gen = NULL;
	const css_computed_content_item *c_item;

	if (style == NULL ||
			css_computed_content(style, &c_item) ==
			CSS_CONTENT_NORMAL) {
		/* No pseudo element */
		return;
	}

	/* create box for this element */
	if (css_computed_display(style, n->parent == NULL) ==
			CSS_DISPLAY_BLOCK) {
		/* currently only support block level after elements */

		/** \todo Not wise to drop const from the computed style */ 
		gen = box_create(NULL, (css_computed_style *) style,
				false, NULL, NULL, NULL, NULL, content);
		if (gen == NULL) {
			return;
		}

		/* set box type from computed display */
		gen->type = box_map[css_computed_display(
				style, n->parent == NULL)];

		box_add_child(box, gen);
	}
}


/**
 * Construct the box tree for an XML text node.
 *
 * \param  n		 XML node of type XML_TEXT_NODE
 * \param  content	 content of type CONTENT_HTML that is being processed
 * \param  parent_style  style at this point in xml tree
 * \param  parent	 parent in box tree
 * \param  inline_container  current inline container box, or 0, updated to
 *			 new current inline container on exit
 * \param  href		 current link URL, or 0 if not in a link
 * \param  target	 current link target, or 0 if none
 * \param  title	 current title, or 0 if none
 * \return  true on success, false on memory exhaustion
 */

bool box_construct_text(xmlNode *n, html_content *content,
		const css_computed_style *parent_style,
		struct box *parent, struct box **inline_container,
		char *href, const char *target, char *title)
{
	struct box *box = 0;

	assert(n);
	assert(n->type == XML_TEXT_NODE);
	assert(parent_style);
	assert(parent);
	assert(inline_container);

	if (css_computed_white_space(parent_style) == CSS_WHITE_SPACE_NORMAL ||
			css_computed_white_space(parent_style) == 
			CSS_WHITE_SPACE_NOWRAP) {
		char *text = squash_whitespace((char *) n->content);
		if (!text)
			return false;

		/* if the text is just a space, combine it with the preceding
		 * text node, if any */
		if (text[0] == ' ' && text[1] == 0) {
			if (*inline_container) {
				if ((*inline_container)->last == 0) {
					LOG(("empty inline_container %p",
							*inline_container));
					while (parent->parent &&
							parent->parent->parent)
						parent = parent->parent;
					box_dump(stderr, parent, 0);
				}

				assert((*inline_container)->last != 0);

				(*inline_container)->last->space =
						UNKNOWN_WIDTH;
			}

			free(text);

			return true;
		}

		if (!*inline_container) {
			/* this is the first inline node: make a container */
			*inline_container = box_create(NULL, 0, false, 0, 0, 0,
					0, content);
			if (!*inline_container) {
				free(text);
				return false;
			}

			(*inline_container)->type = BOX_INLINE_CONTAINER;

			box_add_child(parent, *inline_container);
		}

		/** \todo Dropping const here is not clever */ 
		box = box_create(NULL, (css_computed_style *) parent_style,
				false, href, target, title, 0, content);
		if (!box) {
			free(text);
			return false;
		}

		box->type = BOX_TEXT;

		box->text = talloc_strdup(content, text);
		free(text);
		if (!box->text)
			return false;

		box->length = strlen(box->text);

		/* strip ending space char off */
		if (box->length > 1 && box->text[box->length - 1] == ' ') {
			box->space = UNKNOWN_WIDTH;
			box->length--;
		}

		if (css_computed_text_transform(parent_style) != 
				CSS_TEXT_TRANSFORM_NONE)
			box_text_transform(box->text, box->length,
				css_computed_text_transform(parent_style));

		if (css_computed_white_space(parent_style) == 
				CSS_WHITE_SPACE_NOWRAP) {
			unsigned int i;

			for (i = 0; i != box->length &&
						box->text[i] != ' '; ++i)
				; /* no body */

			if (i != box->length) {
				/* there is a space in text block and we
				 * want all spaces to be converted to NBSP
				 */
				/*box->text = cnv_space2nbsp(text);
				if (!box->text) {
					free(text);
					goto no_memory;
				}
				box->length = strlen(box->text);*/
			}
		}

		box_add_child(*inline_container, box);

		if (box->text[0] == ' ') {
			box->length--;

			memmove(box->text, &box->text[1], box->length);

			if (box->prev != NULL)
				box->prev->space = UNKNOWN_WIDTH;
		}

	} else {
		/* white-space: pre */
		char *text = cnv_space2nbsp((char *) n->content);
		char *current;
		enum css_white_space_e white_space =
				css_computed_white_space(parent_style);

		/* note: pre-wrap/pre-line are unimplemented */
		assert(white_space == CSS_WHITE_SPACE_PRE ||
				white_space == CSS_WHITE_SPACE_PRE_LINE ||
				white_space == CSS_WHITE_SPACE_PRE_WRAP);

		if (!text)
			return false;

		if (css_computed_text_transform(parent_style) != 
				CSS_TEXT_TRANSFORM_NONE)
			box_text_transform(text, strlen(text),
				css_computed_text_transform(parent_style));

		current = text;

		/* swallow a single leading new line */
		if (parent->flags & PRE_STRIP) {
			switch (*current) {
			case '\n':
				current++; break;
			case '\r':
				current++;
				if (*current == '\n') current++;
				break;
			}
			parent->flags &= ~PRE_STRIP;
		}

		do {
			size_t len = strcspn(current, "\r\n");
			char old = current[len];

			current[len] = 0;

			if (!*inline_container) {
				*inline_container = box_create(NULL, 0, false,
						0, 0, 0, 0, content);
				if (!*inline_container) {
					free(text);
					return false;
				}

				(*inline_container)->type =
						BOX_INLINE_CONTAINER;

				box_add_child(parent, *inline_container);
			}

			/** \todo Dropping const isn't clever */
			box = box_create(NULL,
					(css_computed_style *) parent_style,
					false, href, target, title, 0, content);
			if (!box) {
				free(text);
				return false;
			}

			box->type = BOX_TEXT;

			box->text = talloc_strdup(content, current);
			if (!box->text) {
				free(text);
				return false;
			}

			box->length = strlen(box->text);

			box_add_child(*inline_container, box);

			current[len] = old;

			current += len;

			if (current[0] == '\r' && current[1] == '\n') {
				current += 2;
				*inline_container = 0;
			} else if (current[0] != 0) {
				current++;
				*inline_container = 0;
			}
		} while (*current);

		free(text);
	}

	return true;
}

/**
 * Get the style for an element.
 *
 * \param  c		   content of type CONTENT_HTML that is being processed
 * \param  parent_style    style at this point in xml tree, or NULL for root
 * \param  n		   node in xml tree
 * \return  the new style, or NULL on memory exhaustion
 */
css_select_results *box_get_style(html_content *c,
		const css_computed_style *parent_style, xmlNode *n)
{
	char *s;
	int pseudo_element;
	css_error error;
	css_stylesheet *inline_style = NULL;
	css_select_results *styles;
	nscss_select_ctx ctx;

	/* Firstly, construct inline stylesheet, if any */
	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "style"))) {
		inline_style = nscss_create_inline_style(
				(uint8_t *) s, strlen(s),
				c->encoding, content__get_url(&c->base), 
				c->quirks != BINDING_QUIRKS_MODE_NONE,
				box_style_alloc, NULL);

		xmlFree(s);

		if (inline_style == NULL)
			return NULL;
	}

	/* Populate selection context */
	ctx.ctx = c->select_ctx;
	ctx.quirks = (c->quirks == BINDING_QUIRKS_MODE_FULL);
	ctx.base_url = c->base_url;

	/* Select partial style for element */
	styles = nscss_get_style(&ctx, n, CSS_MEDIA_SCREEN, inline_style,
			box_style_alloc, NULL);

	/* No longer need inline style */
	if (inline_style != NULL)
		css_stylesheet_destroy(inline_style);

	/* Failed selecting partial style -- bail out */
	if (styles == NULL)
		return NULL;

	/* If there's a parent style, compose with partial to obtain 
	 * complete computed style for element */
	if (parent_style != NULL) {

		/* Complete the computed style, by composing with the parent
		 * element's style */
		error = css_computed_style_compose(parent_style,
				styles->styles[CSS_PSEUDO_ELEMENT_NONE],
				nscss_compute_font_size, NULL,
				styles->styles[CSS_PSEUDO_ELEMENT_NONE]);
		if (error != CSS_OK) {
			css_select_results_destroy(styles);
			return NULL;
		}
	}

	for (pseudo_element = CSS_PSEUDO_ELEMENT_NONE + 1;
			pseudo_element < CSS_PSEUDO_ELEMENT_COUNT;
			pseudo_element++) {

		if (pseudo_element == CSS_PSEUDO_ELEMENT_FIRST_LETTER ||
				pseudo_element == CSS_PSEUDO_ELEMENT_FIRST_LINE)
			/* TODO: Handle first-line and first-letter pseudo
			 *       element computed style completion */
			continue;

		if (styles->styles[pseudo_element] == NULL)
			/* There were no rules concerning this pseudo element */
			continue;

		/* Complete the pseudo element's computed style, by composing
		 * with the base element's style */
		error = css_computed_style_compose(
				styles->styles[CSS_PSEUDO_ELEMENT_NONE],
				styles->styles[pseudo_element],
				nscss_compute_font_size, NULL,
				styles->styles[pseudo_element]);
		if (error != CSS_OK) {
			/* TODO: perhaps this shouldn't be quite so
			 * catastrophic? */
			css_select_results_destroy(styles);
			return NULL;
		}
	}

	return styles;
}


/**
 * Apply the CSS text-transform property to given text for its ASCII chars.
 *
 * \param  s	string to transform
 * \param  len  length of s
 * \param  tt	transform type
 */

void box_text_transform(char *s, unsigned int len, enum css_text_transform_e tt)
{
	unsigned int i;
	if (len == 0)
		return;
	switch (tt) {
		case CSS_TEXT_TRANSFORM_UPPERCASE:
			for (i = 0; i < len; ++i)
				if ((unsigned char) s[i] < 0x80)
					s[i] = ls_toupper(s[i]);
			break;
		case CSS_TEXT_TRANSFORM_LOWERCASE:
			for (i = 0; i < len; ++i)
				if ((unsigned char) s[i] < 0x80)
					s[i] = ls_tolower(s[i]);
			break;
		case CSS_TEXT_TRANSFORM_CAPITALIZE:
			if ((unsigned char) s[0] < 0x80)
				s[0] = ls_toupper(s[0]);
			for (i = 1; i < len; ++i)
				if ((unsigned char) s[i] < 0x80 &&
						ls_isspace(s[i - 1]))
					s[i] = ls_toupper(s[i]);
			break;
		default:
			break;
	}
}


/**
 * \name  Special case element handlers
 *
 * These functions are called by box_construct_element() when an element is
 * being converted, according to the entries in element_table.
 *
 * The parameters are the xmlNode, the content for the document, and a partly
 * filled in box structure for the element.
 *
 * Return true on success, false on memory exhaustion. Set *convert_children
 * to false if children of this element in the XML tree should be skipped (for
 * example, if they have been processed in some special way already).
 *
 * Elements ordered as in the HTML 4.01 specification. Section numbers in
 * brackets [] refer to the spec.
 *
 * \{
 */

/**
 * Document body [7.5.1].
 */

bool box_body(BOX_SPECIAL_PARAMS)
{
	css_color color;

	css_computed_background_color(box->style, &color);
	if (nscss_color_is_transparent(color))
		content->background_colour = NS_TRANSPARENT;
	else
		content->background_colour = nscss_color_to_ns(color);

	return true;
}


/**
 * Forced line break [9.3.2].
 */

bool box_br(BOX_SPECIAL_PARAMS)
{
	box->type = BOX_BR;
	return true;
}

/**
 * Preformatted text [9.3.4].
 */

bool box_pre(BOX_SPECIAL_PARAMS)
{
	box->flags |= PRE_STRIP;
	return true;
}

/**
 * Anchor [12.2].
 */

bool box_a(BOX_SPECIAL_PARAMS)
{
	bool ok;
	char *url;
	xmlChar *s;

	if ((s = xmlGetProp(n, (const xmlChar *) "href"))) {
		ok = box_extract_link((const char *) s,
				content->base_url, &url);
		xmlFree(s);
		if (!ok)
			return false;
		if (url) {
			box->href = talloc_strdup(content, url);
			free(url);
			if (!box->href)
				return false;
		}
	}

	/* name and id share the same namespace */
	if (!box_get_attribute(n, "name", content, &box->id))
		return false;

	/* target frame [16.3] */
	if ((s = xmlGetProp(n, (const xmlChar *) "target"))) {
		if (!strcasecmp((const char *) s, "_blank"))
			box->target = TARGET_BLANK;
		else if (!strcasecmp((const char *) s, "_top"))
			box->target = TARGET_TOP;
		else if (!strcasecmp((const char *) s, "_parent"))
			box->target = TARGET_PARENT;
		else if (!strcasecmp((const char *) s, "_self"))
			/* the default may have been overridden by a
			 * <base target=...>, so this is different to 0 */
			box->target = TARGET_SELF;
		else {
			/* 6.16 says that frame names must begin with [a-zA-Z]
			 * This doesn't match reality, so just take anything */
			box->target = talloc_strdup(content, (const char *) s);
			if (!box->target) {
				xmlFree(s);
				return false;
			}
		}
		xmlFree(s);
	}

	return true;
}


/**
 * Embedded image [13.2].
 */

bool box_image(BOX_SPECIAL_PARAMS)
{
	bool ok;
	char *s, *url;
	xmlChar *alt, *src;
	enum css_width_e wtype;
	enum css_height_e htype;
	css_fixed value = 0;
	css_unit wunit = CSS_UNIT_PX;
	css_unit hunit = CSS_UNIT_PX;

	if (box->style && css_computed_display(box->style, 
			n->parent == NULL) == CSS_DISPLAY_NONE)
		return true;

	/* handle alt text */
	if ((alt = xmlGetProp(n, (const xmlChar *) "alt"))) {
		s = squash_whitespace((const char *) alt);
		xmlFree(alt);
		if (!s)
			return false;
		box->text = talloc_strdup(content, s);
		free(s);
		if (!box->text)
			return false;
		box->length = strlen(box->text);
	}

	/* imagemap associated with this image */
	if (!box_get_attribute(n, "usemap", content, &box->usemap))
		return false;
	if (box->usemap && box->usemap[0] == '#')
		box->usemap++;

	/* get image URL */
	if (!(src = xmlGetProp(n, (const xmlChar *) "src")))
		return true;
	if (!box_extract_link((char *) src, content->base_url, &url))
		return false;
	xmlFree(src);
	if (!url)
		return true;

	/* start fetch */
	ok = html_fetch_object(content, url, box, image_types,
			content->base.available_width, 1000, false);
	free(url);

	wtype = css_computed_width(box->style, &value, &wunit);
	htype = css_computed_height(box->style, &value, &hunit);

	if (wtype == CSS_WIDTH_SET && wunit != CSS_UNIT_PCT &&
			htype == CSS_HEIGHT_SET && hunit != CSS_UNIT_PCT) {
		/* We know the dimensions the image will be shown at before it's
		 * fetched. */
		box->flags |= REPLACE_DIM;
	}

	return ok;
}


/**
 * Generic embedded object [13.3].
 */

bool box_object(BOX_SPECIAL_PARAMS)
{
	struct object_params *params;
	struct object_param *param;
	xmlChar *codebase, *classid, *data;
	xmlNode *c;

	if (box->style && css_computed_display(box->style, 
			n->parent == NULL) == CSS_DISPLAY_NONE)
		return true;

	if (!box_get_attribute(n, "usemap", content, &box->usemap))
		return false;
	if (box->usemap && box->usemap[0] == '#')
		box->usemap++;

	params = talloc(content, struct object_params);
	if (!params)
		return false;
	params->data = 0;
	params->type = 0;
	params->codetype = 0;
	params->codebase = 0;
	params->classid = 0;
	params->params = 0;

	/* codebase, classid, and data are URLs
	 * (codebase is the base for the other two) */
	if ((codebase = xmlGetProp(n, (const xmlChar *) "codebase"))) {
		if (!box_extract_link((char *) codebase,
				content->base_url,
				&params->codebase))
			return false;
		xmlFree(codebase);
	}
	if (!params->codebase)
		params->codebase = content->base_url;

	if ((classid = xmlGetProp(n, (const xmlChar *) "classid"))) {
		if (!box_extract_link((char *) classid, params->codebase,
				&params->classid))
			return false;
		xmlFree(classid);
	}

	if ((data = xmlGetProp(n, (const xmlChar *) "data"))) {
		if (!box_extract_link((char *) data, params->codebase,
				&params->data))
			return false;
		xmlFree(data);
	}

	if (!params->classid && !params->data)
		/* nothing to embed; ignore */
		return true;

	/* Don't include ourself */
	if (params->classid && strcmp(content->base_url, params->classid) == 0)
		return true;

	if (params->data && strcmp(content->base_url, params->data) == 0)
		return true;

	/* codetype and type are MIME types */
	if (!box_get_attribute(n, "codetype", params, &params->codetype))
		return false;
	if (!box_get_attribute(n, "type", params, &params->type))
		return false;

	/* classid && !data => classid is used (consult codetype)
	 * (classid || !classid) && data => data is used (consult type)
	 * !classid && !data => invalid; ignored */

	if (params->classid && !params->data && params->codetype &&
			content_factory_type_from_mime_type(params->codetype) ==
			CONTENT_NONE)
		/* can't handle this MIME type */
		return true;

	if (params->data && params->type &&
			content_factory_type_from_mime_type(params->type) == 
			CONTENT_NONE)
		/* can't handle this MIME type */
		return true;

	/* add parameters to linked list */
	for (c = n->children; c; c = c->next) {
		if (c->type != XML_ELEMENT_NODE)
			continue;
		if (strcmp((const char *) c->name, "param") != 0)
			/* The first non-param child is the start of the alt
			 * html. Therefore, we should break out of this loop. */
			break;

		param = talloc(params, struct object_param);
		if (!param)
			return false;
		param->name = 0;
		param->value = 0;
		param->type = 0;
		param->valuetype = 0;
		param->next = 0;

		if (!box_get_attribute(c, "name", param, &param->name))
			return false;
		if (!box_get_attribute(c, "value", param, &param->value))
			return false;
		if (!box_get_attribute(c, "type", param, &param->type))
			return false;
		if (!box_get_attribute(c, "valuetype", param,
				&param->valuetype))
			return false;
		if (!param->valuetype) {
			param->valuetype = talloc_strdup(param, "data");
			if (!param->valuetype)
				return false;
		}

		param->next = params->params;
		params->params = param;
	}

	box->object_params = params;

	/* start fetch (MIME type is ok or not specified) */
	if (!html_fetch_object(content,
			params->data ? params->data : params->classid,
			box, CONTENT_ANY, content->base.available_width, 1000, 
			false))
		return false;

	*convert_children = false;
	return true;
}


/**
 * Window subdivision [16.2.1].
 */

bool box_frameset(BOX_SPECIAL_PARAMS)
{
	bool ok;

	if (content->frameset) {
		LOG(("Error: multiple framesets in document."));
		/* Don't convert children */
		if (convert_children)
			*convert_children = false;
		/* And ignore this spurious frameset */
		box->type = BOX_NONE;
		return true;
	}

	content->frameset = talloc_zero(content, struct content_html_frames);
	if (!content->frameset)
		return false;

	ok = box_create_frameset(content->frameset, n, content);
	if (ok)
		box->type = BOX_NONE;

	if (convert_children)
		*convert_children = false;
	return ok;
}

bool box_create_frameset(struct content_html_frames *f, xmlNode *n,
		html_content *content) {
	unsigned int row, col, index, i;
	unsigned int rows = 1, cols = 1;
	char *s, *url;
	struct frame_dimension *row_height = 0, *col_width = 0;
	xmlNode *c;
	struct content_html_frames *frame;
	bool default_border = true;
	colour default_border_colour = 0x000000;

	/* parse rows and columns */
	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "rows"))) {
		row_height = box_parse_multi_lengths(s, &rows);
		xmlFree(s);
		if (!row_height)
			return false;
	} else {
		row_height = calloc(1, sizeof(struct frame_dimension));
		if (!row_height)
			return false;
		row_height->value = 100;
		row_height->unit = FRAME_DIMENSION_PERCENT;
	}

	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "cols"))) {
		col_width = box_parse_multi_lengths(s, &cols);
		xmlFree(s);
		if (!col_width)
			return false;
	} else {
		col_width = calloc(1, sizeof(struct frame_dimension));
		if (!col_width)
			return false;
		col_width->value = 100;
		col_width->unit = FRAME_DIMENSION_PERCENT;
	}

	/* common extension: border="0|1" to control all children */
	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "border"))) {
		if ((s[0] == '0') && (s[1] == '\0'))
			default_border = false;
		xmlFree(s);
	}
	/* common extension: frameborder="yes|no" to control all children */
	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "frameborder"))) {
	  	if (!strcasecmp(s, "no"))
	  		default_border = false;
		xmlFree(s);
	}
	/* common extension: bordercolor="#RRGGBB|<named colour>" to control
	 *all children */
	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "bordercolor"))) {
		css_color color;

		if (nscss_parse_colour((const char *) s, &color))
			default_border_colour = nscss_color_to_ns(color);

		xmlFree(s);
	}

	/* update frameset and create default children */
	f->cols = cols;
	f->rows = rows;
	f->scrolling = SCROLLING_NO;
	f->children = talloc_array(content, struct content_html_frames,
								(rows * cols));
	for (row = 0; row < rows; row++) {
		for (col = 0; col < cols; col++) {
			index = (row * cols) + col;
			frame = &f->children[index];
			frame->cols = 0;
			frame->rows = 0;
			frame->width = col_width[col];
			frame->height = row_height[row];
			frame->margin_width = 0;
			frame->margin_height = 0;
			frame->name = NULL;
			frame->url = NULL;
			frame->no_resize = false;
			frame->scrolling = SCROLLING_AUTO;
			frame->border = default_border;
			frame->border_colour = default_border_colour;
			frame->children = NULL;
		}
	}
	free(col_width);
	free(row_height);

	/* create the frameset windows */
	c = n->children;
	for (row = 0; c && row < rows; row++) {
		for (col = 0; c && col < cols; col++) {
			while (c && !(c->type == XML_ELEMENT_NODE && (
				strcmp((const char *) c->name, "frame") == 0 ||
				strcmp((const char *) c->name, "frameset") == 0
					)))
				c = c->next;
			if (!c)
				break;

			/* get current frame */
			index = (row * cols) + col;
			frame = &f->children[index];

			/* nest framesets */
			if (strcmp((const char *) c->name, "frameset") == 0) {
				frame->border = 0;
				if (!box_create_frameset(frame, c, content))
					return false;
				c = c->next;
				continue;
			}

			/* get frame URL (not required) */
			url = NULL;
			if ((s = (char *) xmlGetProp(c,
					(const xmlChar *) "src"))) {
				box_extract_link(s, content->base_url, &url);
				xmlFree(s);
			}

			/* copy url */
			if (url) {
			  	/* no self-references */
			  	if (strcmp(content->base_url, url))
					frame->url = talloc_strdup(content,
									url);
				free(url);
				url = NULL;
			}

			/* fill in specified values */
			if ((s = (char *) xmlGetProp(c,
					(const xmlChar *) "name"))) {
				frame->name = talloc_strdup(content, s);
				xmlFree(s);
			}
			frame->no_resize = xmlHasProp(c,
					(const xmlChar *) "noresize") != NULL;
			if ((s = (char *) xmlGetProp(c,
					(const xmlChar *) "frameborder"))) {
				i = atoi(s);
				frame->border = (i != 0);
				xmlFree(s);
			}
			if ((s = (char *) xmlGetProp(c,
					(const xmlChar *) "scrolling"))) {
				if (!strcasecmp(s, "yes"))
					frame->scrolling = SCROLLING_YES;
				else if (!strcasecmp(s, "no"))
					frame->scrolling = SCROLLING_NO;
				xmlFree(s);
			}
			if ((s = (char *) xmlGetProp(c,
					(const xmlChar *) "marginwidth"))) {
				frame->margin_width = atoi(s);
				xmlFree(s);
			}
			if ((s = (char *) xmlGetProp(c,
					(const xmlChar *) "marginheight"))) {
				frame->margin_height = atoi(s);
				xmlFree(s);
			}
			if ((s = (char *) xmlGetProp(c, (const xmlChar *)
							"bordercolor"))) {
				css_color color;

				if (nscss_parse_colour((const char *) s, 
						&color))
					frame->border_colour =
						nscss_color_to_ns(color);

				xmlFree(s);
			}

			/* advance */
			c = c->next;
		}
	}

	return true;
}


/**
 * Inline subwindow [16.5].
 */

bool box_iframe(BOX_SPECIAL_PARAMS)
{
	char *url, *s;
	struct content_html_iframe *iframe;
	int i;

	if (box->style && css_computed_display(box->style, 
			n->parent == NULL) == CSS_DISPLAY_NONE)
		return true;

	if (box->style && css_computed_visibility(box->style) ==
			CSS_VISIBILITY_HIDDEN)
		/* Don't create iframe discriptors for invisible iframes
		 * TODO: handle hidden iframes at browser_window generation
		 * time instead? */
		return true;

	/* get frame URL */
	if (!(s = (char *) xmlGetProp(n,
			(const xmlChar *) "src")))
		return true;
	if (!box_extract_link(s, content->base_url, &url)) {
		xmlFree(s);
		return false;
	}
	xmlFree(s);
	if (!url)
		return true;

	/* don't include ourself */
	if (strcmp(content->base_url, url) == 0) {
		free(url);
		return true;
	}

	/* create a new iframe */
	iframe = talloc(content, struct content_html_iframe);
	if (!iframe) {
		free(url);
		return false;
	}
	iframe->box = box;
	iframe->margin_width = 0;
	iframe->margin_height = 0;
	iframe->name = NULL;
	iframe->url = talloc_strdup(content, url);
	iframe->scrolling = SCROLLING_AUTO;
	iframe->border = true;

	/* Add this iframe to the linked list of iframes */
	iframe->next = content->iframe;
	content->iframe = iframe;

	/* fill in specified values */
	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "name"))) {
		iframe->name = talloc_strdup(content, s);
		xmlFree(s);
	}
	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "frameborder"))) {
		i = atoi(s);
		iframe->border = (i != 0);
		xmlFree(s);
	}
	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "bordercolor"))) {
		css_color color;

		if (nscss_parse_colour(s, &color))
			iframe->border_colour = nscss_color_to_ns(color);

		xmlFree(s);
	}
	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "scrolling"))) {
		if (!strcasecmp(s, "yes"))
			iframe->scrolling = SCROLLING_YES;
		else if (!strcasecmp(s, "no"))
			iframe->scrolling = SCROLLING_NO;
		xmlFree(s);
	}
	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "marginwidth"))) {
		iframe->margin_width = atoi(s);
		xmlFree(s);
	}
	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "marginheight"))) {
		iframe->margin_height = atoi(s);
		xmlFree(s);
	}

	/* release temporary memory */
	free(url);

	/* box */
	box->type = BOX_INLINE_BLOCK;
	assert(box->style);

	/* Showing iframe, so don't show alternate content */
	if (convert_children)
		*convert_children = false;
	return true;
}


/**
 * Form control [17.4].
 */

bool box_input(BOX_SPECIAL_PARAMS)
{
	struct form_control *gadget = NULL;
	char *s, *type, *url;
	url_func_result res;

	type = (char *) xmlGetProp(n, (const xmlChar *) "type");

	gadget = binding_get_control_for_node(content->parser_binding, n);
	if (!gadget)
		goto no_memory;
	box->gadget = gadget;
	gadget->box = box;

	if (type && strcasecmp(type, "password") == 0) {
		if (!box_input_text(n, content, box, 0, true))
			goto no_memory;
	} else if (type && strcasecmp(type, "file") == 0) {
		box->type = BOX_INLINE_BLOCK;
	} else if (type && strcasecmp(type, "hidden") == 0) {
		/* no box for hidden inputs */
		box->type = BOX_NONE;
	} else if (type && (strcasecmp(type, "checkbox") == 0 ||
			strcasecmp(type, "radio") == 0)) {
	} else if (type && (strcasecmp(type, "submit") == 0 ||
			strcasecmp(type, "reset") == 0 ||
			strcasecmp(type, "button") == 0)) {
		struct box *inline_container, *inline_box;

		if (!box_button(n, content, box, 0))
			goto no_memory;

		inline_container = box_create(NULL, 0, false, 0, 0, 0, 0,
				content);
		if (!inline_container)
			goto no_memory;

		inline_container->type = BOX_INLINE_CONTAINER;

		inline_box = box_create(NULL, box->style, false, 0, 0,
				box->title, 0, content);
		if (!inline_box)
			goto no_memory;

		inline_box->type = BOX_TEXT;

		if (box->gadget->value != NULL)
			inline_box->text = talloc_strdup(content,
					box->gadget->value);
		else if (box->gadget->type == GADGET_SUBMIT)
			inline_box->text = talloc_strdup(content,
					messages_get("Form_Submit"));
		else if (box->gadget->type == GADGET_RESET)
			inline_box->text = talloc_strdup(content,
					messages_get("Form_Reset"));
		else
			inline_box->text = talloc_strdup(content, "Button");

		if (!inline_box->text)
			goto no_memory;

		inline_box->length = strlen(inline_box->text);

		box_add_child(inline_container, inline_box);

		box_add_child(box, inline_container);
	} else if (type && strcasecmp(type, "image") == 0) {
		gadget->type = GADGET_IMAGE;

		if (box->style && css_computed_display(box->style,
				n->parent == NULL) != CSS_DISPLAY_NONE) {
			if ((s = (char *) xmlGetProp(n,
					(const xmlChar*) "src"))) {
				res = url_join(s, content->base_url, &url);
				xmlFree(s);
				/* if url is equivalent to the parent's url,
				 * we've got infinite inclusion. stop it here
				 * also bail if url_join failed.
				 */
				if (res == URL_FUNC_OK &&
						strcasecmp(url,
						content->base_url) != 0) {
					if (!html_fetch_object(content, url,
							box, image_types,
							content->base.
							available_width,
							1000, false)) {
						free(url);
						goto no_memory;
					}
				}
				free(url);
			}
		}
	} else {
		/* the default type is "text" */
		if (!box_input_text(n, content, box, 0, false))
			goto no_memory;
	}

	if (type)
		xmlFree(type);

	*convert_children = false;
	return true;

no_memory:
	if (type)
		xmlFree(type);
	return false;
}


/**
 * Helper function for box_input().
 */

bool box_input_text(BOX_SPECIAL_PARAMS, bool password)
{
	struct box *inline_container, *inline_box;

	box->type = BOX_INLINE_BLOCK;

	inline_container = box_create(NULL, 0, false, 0, 0, 0, 0, content);
	if (!inline_container)
		return false;
	inline_container->type = BOX_INLINE_CONTAINER;
	inline_box = box_create(NULL, box->style, false, 0, 0, box->title, 0,
			content);
	if (!inline_box)
		return false;
	inline_box->type = BOX_TEXT;
	if (password) {
		inline_box->length = strlen(box->gadget->value);
		inline_box->text = talloc_array(content, char,
				inline_box->length + 1);
		if (!inline_box->text)
			return false;
		memset(inline_box->text, '*', inline_box->length);
		inline_box->text[inline_box->length] = '\0';
	} else {
		/* replace spaces/TABs with hard spaces to prevent line
		 * wrapping */
		char *text = cnv_space2nbsp(box->gadget->value);
		if (!text)
			return false;
		inline_box->text = talloc_strdup(content, text);
		free(text);
		if (!inline_box->text)
			return false;
		inline_box->length = strlen(inline_box->text);
	}
	box_add_child(inline_container, inline_box);
	box_add_child(box, inline_container);

	return true;
}


/**
 * Push button [17.5].
 */

bool box_button(BOX_SPECIAL_PARAMS)
{
	struct form_control *gadget;

	gadget = binding_get_control_for_node(content->parser_binding, n);
	if (!gadget)
		return false;

	box->gadget = gadget;
	gadget->box = box;

	box->type = BOX_INLINE_BLOCK;

	/* Just render the contents */

	return true;
}


/**
 * Option selector [17.6].
 */

bool box_select(BOX_SPECIAL_PARAMS)
{
	struct box *inline_container;
	struct box *inline_box;
	struct form_control *gadget;
	xmlNode *c, *c2;

	gadget = binding_get_control_for_node(content->parser_binding, n);
	if (!gadget)
		return false;

	for (c = n->children; c; c = c->next) {
		if (strcmp((const char *) c->name, "option") == 0) {
			if (!box_select_add_option(gadget, c))
				goto no_memory;
		} else if (strcmp((const char *) c->name, "optgroup") == 0) {
			for (c2 = c->children; c2; c2 = c2->next) {
				if (strcmp((const char *) c2->name,
						"option") == 0) {
					if (!box_select_add_option(gadget, c2))
						goto no_memory;
				}
			}
		}
	}

	if (gadget->data.select.num_items == 0) {
		/* no options: ignore entire select */
		return true;
	}

	box->type = BOX_INLINE_BLOCK;
	box->gadget = gadget;
	gadget->box = box;

	inline_container = box_create(NULL, 0, false, 0, 0, 0, 0, content);
	if (!inline_container)
		goto no_memory;
	inline_container->type = BOX_INLINE_CONTAINER;
	inline_box = box_create(NULL, box->style, false, 0, 0, box->title, 0,
			content);
	if (!inline_box)
		goto no_memory;
	inline_box->type = BOX_TEXT;
	box_add_child(inline_container, inline_box);
	box_add_child(box, inline_container);

	if (!gadget->data.select.multiple &&
			gadget->data.select.num_selected == 0) {
		gadget->data.select.current = gadget->data.select.items;
		gadget->data.select.current->initial_selected =
			gadget->data.select.current->selected = true;
		gadget->data.select.num_selected = 1;
	}

	if (gadget->data.select.num_selected == 0)
		inline_box->text = talloc_strdup(content,
				messages_get("Form_None"));
	else if (gadget->data.select.num_selected == 1)
		inline_box->text = talloc_strdup(content,
				gadget->data.select.current->text);
	else
		inline_box->text = talloc_strdup(content,
				messages_get("Form_Many"));
	if (!inline_box->text)
		goto no_memory;

	inline_box->length = strlen(inline_box->text);

	*convert_children = false;
	return true;

no_memory:
	return false;
}


/**
 * Add an option to a form select control (helper function for box_select()).
 *
 * \param  control  select containing the option
 * \param  n	    xml element node for <option>
 * \return  true on success, false on memory exhaustion
 */

bool box_select_add_option(struct form_control *control, xmlNode *n)
{
	char *value = 0;
	char *text = 0;
	char *text_nowrap = 0;
	bool selected;
	xmlChar *content;
	char *s;

	content = xmlNodeGetContent(n);
	if (!content)
		goto no_memory;
	text = squash_whitespace((const char *) content);
	xmlFree(content);
	if (!text)
		goto no_memory;

	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "value"))) {
		value = strdup(s);
		xmlFree(s);
	} else
		value = strdup(text);
	if (!value)
		goto no_memory;

	selected = xmlHasProp(n, (const xmlChar *) "selected") != NULL;

	/* replace spaces/TABs with hard spaces to prevent line wrapping */
	text_nowrap = cnv_space2nbsp(text);
	if (!text_nowrap)
		goto no_memory;

	if (!form_add_option(control, value, text_nowrap, selected))
		goto no_memory;

	free(text);

	return true;

no_memory:
	free(value);
	free(text);
	free(text_nowrap);
	return false;
}


/**
 * Multi-line text field [17.7].
 */

bool box_textarea(BOX_SPECIAL_PARAMS)
{
	/* A textarea is an INLINE_BLOCK containing a single INLINE_CONTAINER,
	 * which contains the text as runs of TEXT separated by BR. There is
	 * at least one TEXT. The first and last boxes are TEXT.
	 * Consecutive BR may not be present. These constraints are satisfied
	 * by using a 0-length TEXT for blank lines. */

	xmlChar *current, *string;
	xmlNode *n2;
	xmlBufferPtr buf;
	xmlParserCtxtPtr ctxt;
	struct box *inline_container, *inline_box, *br_box;
	char *s;
	size_t len;

	box->type = BOX_INLINE_BLOCK;
	box->gadget = binding_get_control_for_node(content->parser_binding, n);
	if (!box->gadget)
		return false;
	box->gadget->box = box;

	inline_container = box_create(NULL, 0, false, 0, 0, box->title, 0,
			content);
	if (!inline_container)
		return false;
	inline_container->type = BOX_INLINE_CONTAINER;
	box_add_child(box, inline_container);

	/** \todo Is it really necessary to reparse the content of a
	 * textarea element to remove entities? Hubbub will do that for us.
	 */
	n2 = n->children;
	buf = xmlBufferCreate();
	while(n2) {
		int ret = xmlNodeDump(buf, n2->doc, n2, 0, 0);
		if (ret == -1) {
			xmlBufferFree(buf);
			return false;
		}
		n2 = n2->next;
	}

	ctxt = xmlCreateDocParserCtxt(buf->content);
	string = current = NULL;
	if (ctxt) {
		string = current = xmlStringDecodeEntities(ctxt,
				buf->content,
				XML_SUBSTITUTE_REF,
				0, 0, 0);
		xmlFreeParserCtxt(ctxt);
	}

	if (!string) {
		/* If we get here, either the parser context failed to be
		 * created or we were unable to decode the entities in the
		 * buffer. Therefore, try to create a blank string in order
		 * to recover. */
		string = current = xmlStrdup((const xmlChar *) "");
		if (!string) {
			xmlBufferFree(buf);
			return false;
		}
	}

	while (1) {
		/* BOX_TEXT */
		len = strcspn((const char *) current, "\r\n");
		s = talloc_strndup(content, (const char *) current, len);
		if (!s) {
			xmlFree(string);
			xmlBufferFree(buf);
			return false;
		}

		inline_box = box_create(NULL, box->style, false, 0, 0,
				box->title, 0, content);
		if (!inline_box) {
			xmlFree(string);
			xmlBufferFree(buf);
			return false;
		}
		inline_box->type = BOX_TEXT;
		inline_box->text = s;
		inline_box->length = len;
		box_add_child(inline_container, inline_box);

		current += len;
		if (current[0] == 0)
			/* finished */
			break;

		/* BOX_BR */
		br_box = box_create(NULL, box->style, false, 0, 0, box->title,
				0, content);
		if (!br_box) {
			xmlFree(string);
			xmlBufferFree(buf);
			return false;
		}
		br_box->type = BOX_BR;
		box_add_child(inline_container, br_box);

		if (current[0] == '\r' && current[1] == '\n')
			current += 2;
		else
			current++;
	}

	xmlFree(string);
	xmlBufferFree(buf);

	*convert_children = false;
	return true;
}


/**
 * Embedded object (not in any HTML specification:
 * see http://wp.netscape.com/assist/net_sites/new_html3_prop.html )
 */

bool box_embed(BOX_SPECIAL_PARAMS)
{
	struct object_params *params;
	struct object_param *param;
	xmlChar *src;
	xmlAttr *a;

	if (box->style && css_computed_display(box->style,
			n->parent == NULL) == CSS_DISPLAY_NONE)
		return true;

	params = talloc(content, struct object_params);
	if (!params)
		return false;
	params->data = 0;
	params->type = 0;
	params->codetype = 0;
	params->codebase = 0;
	params->classid = 0;
	params->params = 0;

	/* src is a URL */
	if (!(src = xmlGetProp(n, (const xmlChar *) "src")))
		return true;
	if (!box_extract_link((char *) src, content->base_url, &params->data))
		return false;
	xmlFree(src);
	if (!params->data)
		return true;

	/* Don't include ourself */
	if (strcmp(content->base_url, params->data) == 0)
		return true;

	/* add attributes as parameters to linked list */
	for (a = n->properties; a; a = a->next) {
		if (strcasecmp((const char *) a->name, "src") == 0)
			continue;
		if (!a->children || !a->children->content)
			continue;

		param = talloc(content, struct object_param);
		if (!param)
			return false;
		param->name = talloc_strdup(content, (const char *) a->name);
		param->value = talloc_strdup(content,
				(char *) a->children->content);
		param->type = 0;
		param->valuetype = talloc_strdup(content, "data");
		param->next = 0;

		if (!param->name || !param->value || !param->valuetype)
			return false;

		param->next = params->params;
		params->params = param;
	}

	box->object_params = params;

	/* start fetch */
	return html_fetch_object(content, params->data, box, CONTENT_ANY,
			content->base.available_width, 1000, false);
}

/**
 * \}
 */


/**
 * Get the value of an XML element's attribute.
 *
 * \param  n	      xmlNode, of type XML_ELEMENT_NODE
 * \param  attribute  name of attribute
 * \param  context    talloc context for result buffer
 * \param  value      updated to value, if the attribute is present
 * \return  true on success, false if attribute present but memory exhausted
 *
 * Note that returning true does not imply that the attribute was found. If the
 * attribute was not found, *value will be unchanged.
 */

bool box_get_attribute(xmlNode *n, const char *attribute,
		void *context, char **value)
{
	xmlChar *s = xmlGetProp(n, (const xmlChar *) attribute);
	if (!s)
		return true;
	*value = talloc_strdup(context, (const char *) s);
	xmlFree(s);
	if (!*value)
		return false;
	return true;
}


/**
 * Extract a URL from a relative link, handling junk like whitespace and
 * attempting to read a real URL from "javascript:" links.
 *
 * \param  rel	   relative URL taken from page
 * \param  base	   base for relative URLs
 * \param  result  updated to target URL on heap, unchanged if extract failed
 * \return  true on success, false on memory exhaustion
 */

bool box_extract_link(const char *rel, const char *base, char **result)
{
	char *s, *s1, *apos0 = 0, *apos1 = 0, *quot0 = 0, *quot1 = 0;
	unsigned int i, j, end;
	url_func_result res;

	s1 = s = malloc(3 * strlen(rel) + 1);
	if (!s)
		return false;

	/* copy to s, removing white space and control characters */
	for (i = 0; rel[i] && isspace(rel[i]); i++)
		;
	for (end = strlen(rel); end != i && isspace(rel[end - 1]); end--)
		;
	for (j = 0; i != end; i++) {
		if ((unsigned char) rel[i] < 0x20) {
			; /* skip control characters */
		} else if (rel[i] == ' ') {
			s[j++] = '%';
			s[j++] = '2';
			s[j++] = '0';
		} else {
			s[j++] = rel[i];
		}
	}
	s[j] = 0;

	/* extract first quoted string out of "javascript:" link */
	if (strncmp(s, "javascript:", 11) == 0) {
		apos0 = strchr(s, '\'');
		if (apos0)
			apos1 = strchr(apos0 + 1, '\'');
		quot0 = strchr(s, '"');
		if (quot0)
			quot1 = strchr(quot0 + 1, '"');
		if (apos0 && apos1 && (!quot0 || !quot1 || apos0 < quot0)) {
			*apos1 = 0;
			s1 = apos0 + 1;
		} else if (quot0 && quot1) {
			*quot1 = 0;
			s1 = quot0 + 1;
		}
	}

	/* construct absolute URL */
	res = url_join(s1, base, result);
	free(s);
	if (res == URL_FUNC_NOMEM)
		return false;
	else if (res == URL_FUNC_FAILED)
		return true;

	return true;
}


/**
 * Parse a multi-length-list, as defined by HTML 4.01.
 *
 * \param  s	    string to parse
 * \param  count    updated to number of entries
 * \return  array of struct box_multi_length, or 0 on memory exhaustion
 */

struct frame_dimension *box_parse_multi_lengths(const char *s,
		unsigned int *count)
{
	char *end;
	unsigned int i, n;
	struct frame_dimension *length;

	for (i = 0, n = 1; s[i]; i++)
		if (s[i] == ',')
			n++;

	length = calloc(n, sizeof(struct frame_dimension));
	if (!length)
		return NULL;

	for (i = 0; i != n; i++) {
		while (isspace(*s))
			s++;
		length[i].value = strtof(s, &end);
		if (length[i].value <= 0)
			length[i].value = 1;
		s = end;
		switch (*s) {
			case '%':
				length[i].unit = FRAME_DIMENSION_PERCENT;
				break;
			case '*':
				length[i].unit = FRAME_DIMENSION_RELATIVE;
				break;
			default:
				length[i].unit = FRAME_DIMENSION_PIXELS;
				break;
		}
		while (*s && *s != ',')
			s++;
		if (*s == ',')
			s++;
	}

	*count = n;
	return length;
}

