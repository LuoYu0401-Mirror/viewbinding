// SPDX-License-Identifier: GPL-3.0-or-later

/**
  * viewbinding: A tool to generate view binding code for GTK applications
  * Copyright (C) 2025- LuoYu0401 luoyu0401@139.com
  *
  * This program is free software: you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation, either version 3 of the License, or
  * version.
  *
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  * GNU General Public License for more details.
  *
  * You should have received a copy of the GNU General Public License
  * along with this program.  If not, see <https://www.gnu.org/licenses/>.
  **/

#include <glib.h>
#include <gio/gio.h>

typedef struct {
	gchar *class;
	gchar *id;
} ClassId;

typedef struct {
	gchar *element_name;

	void (*handle_attribute)(const gchar *element_name,
				 const gchar **attribute_name,
				 const gchar **attribute_value,
				 gpointer user_data);

	void (*generate_code)(const gchar *base_name, gpointer user_data);

	GDestroyNotify destroy_user_data;
	gpointer user_data;
} ViewBindingParser;

static void parse_arguments(int argc, char *argv[]);

static void check_arguments(void);

static void read_and_parse_xml_file(const gchar *file_name);

static void start(GMarkupParseContext *context, const gchar *element_name,
		  const gchar **attribute_names, const gchar **attribute_values,
		  gpointer user_data, GError **error);

static void destroy_view_binding_parser(ViewBindingParser *parser);

static void destroy_list_class_id(GList **class_id_list);

static void destroy_class_id(ClassId *class_id);

static void destroy_signal_array(GArray **signal_array);

static void handle_object_attribute(const gchar *element_name,
				    const gchar **attribute_names,
				    const gchar **attribute_values,
				    gpointer user_data);

static void handle_signal_attribute(const gchar *element_name,
				    const gchar **attribute_names,
				    const gchar **attribute_values,
				    gpointer user_data);

static void generate_code(GHashTable *view_binding_parser_map,
			  const gchar *file_name);

static void generate_object_code(const gchar *base_name, gpointer user_data);

static void generate_signal_code(const gchar *base_name, gpointer user_data);

static void hash_table_for_each(gpointer key, gpointer value,
				gpointer user_data);

static gchar *application_id = NULL;
static gchar *directory = NULL;
static gchar *output_directory = NULL;

static GString *output_buffer = NULL;

static GOptionEntry entries[] = {
	{ "application-id", 'a', 0, G_OPTION_ARG_STRING, &application_id,
	  "The application ID", "ID" },
	{ "directory", 'd', 0, G_OPTION_ARG_STRING, &directory,
	  "The directory to scan for UI files", "DIR" },
	{ "output-directory", 'o', 0, G_OPTION_ARG_STRING, &output_directory,
	  "The output directory for generated files", "DIR" },
	{ NULL }
};

static ViewBindingParser class_parser = {
	.element_name = "object",
	.handle_attribute = handle_object_attribute,
	.generate_code = generate_object_code,
	.destroy_user_data = (GDestroyNotify)destroy_list_class_id,
	.user_data = NULL,
};

static ViewBindingParser signal_parser = {
	.element_name = "signal",
	.handle_attribute = handle_signal_attribute,
	.generate_code = generate_signal_code,
	.destroy_user_data = (GDestroyNotify)destroy_signal_array,
	.user_data = NULL,
};

static GMarkupParser xml_parser = {
	.start_element = start,
};

int main(int argc, char *argv[])
{
	parse_arguments(argc, argv);
	check_arguments();

	// Scan the directory for .ui files
	g_autoptr(GDir) dir = g_dir_open(directory, 0, NULL);
	const gchar *name = NULL;
	while ((name = g_dir_read_name(dir)) != NULL) {
		if (!g_str_has_suffix(name, ".ui")) {
			continue;
		}
		read_and_parse_xml_file(name);
	}

	// Clean up
	if (application_id)
		g_free(application_id);
	if (directory)
		g_free(directory);
	if (output_directory)
		g_free(output_directory);
	return 0;
}

static void parse_arguments(int argc, char *argv[])
{
	g_autoptr(GOptionContext)
		context = g_option_context_new("- View Binding Code Generator");
	g_autoptr(GError) error = NULL;

	g_option_context_add_main_entries(context, entries, NULL);
	if (!g_option_context_parse(context, &argc, &argv, &error)) {
		g_printerr("Error parsing options: %s\n", error->message);
	}
}

static void check_arguments(void)
{
	g_autoptr(GError) error = NULL;
	if (application_id == NULL) {
		g_printerr("Error: --application-id is required.\n");
		exit(EXIT_FAILURE);
	}
	g_autoptr(GRegex) regex = g_regex_new("^[a-zA-Z][\\w]+_[\\w]+_[\\w]+$",
					      G_REGEX_OPTIMIZE, 0, &error);
	if (!g_regex_match(regex, application_id, 0, NULL)) {
		g_printerr(
			"application-id '%s' is not valid. It must be in the format com_example_AppName\n",
			application_id);
		exit(EXIT_FAILURE);
	}

	if (directory == NULL) {
		g_printerr("Error: --directory is required.\n");
		exit(EXIT_FAILURE);
	}
	if (!g_file_test(directory, G_FILE_TEST_IS_DIR)) {
		g_printerr(
			"Error: --directory '%s' is not a valid directory.\n",
			directory);
		exit(EXIT_FAILURE);
	}

	if (output_directory == NULL) {
		g_printerr("Error: --output-directory is required.\n");
		exit(EXIT_FAILURE);
	}
	if (g_file_test(output_directory, G_FILE_TEST_EXISTS)) {
		if (!g_file_test(output_directory, G_FILE_TEST_IS_DIR)) {
			g_printerr(
				"Error: --output-directory '%s' is not a valid directory.\n",
				output_directory);
			exit(EXIT_FAILURE);
		}
	} else {
		// create the output directory
		if (g_mkdir_with_parents(output_directory, 0755) != 0) {
			g_printerr(
				"Error: could not create output directory '%s'.\n",
				output_directory);
			exit(EXIT_FAILURE);
		}
	}
}

static void read_and_parse_xml_file(const gchar *file_name)
{
	g_autoptr(GMarkupParseContext) context = NULL;
	g_autoptr(GHashTable) view_binding_parser_map = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *file_path =
		g_build_filename(directory, file_name, NULL);
	g_autofree gchar *xml_content = NULL;
	gsize file_size = 0;

	g_file_get_contents(file_path, &xml_content, &file_size, &error);
	if (error) {
		g_printerr("Error reading file %s: %s\n", file_path,
			   error->message);
		g_error_free(error);
		error = NULL;
		return;
	}

	view_binding_parser_map = g_hash_table_new_full(
		g_str_hash, g_str_equal, NULL,
		(GDestroyNotify)destroy_view_binding_parser);
	g_hash_table_insert(view_binding_parser_map, class_parser.element_name,
			    &class_parser);
	g_hash_table_insert(view_binding_parser_map, signal_parser.element_name,
			    &signal_parser);

	context = g_markup_parse_context_new(&xml_parser, 0,
					     &view_binding_parser_map, NULL);

	if (!g_markup_parse_context_parse(context, xml_content, file_size,
					  &error)) {
		g_printerr("Error parsing XML file %s: %s\n", file_path,
			   error ? error->message : "Unknown error");
		if (error) {
			g_error_free(error);
			error = NULL;
		}
		return;
	}

	generate_code(view_binding_parser_map, file_name);
}

static void start(GMarkupParseContext *context, const gchar *element_name,
		  const gchar **attribute_names, const gchar **attribute_values,
		  gpointer user_data, GError **error)
{
	GHashTable **view_binding_parser_map = (GHashTable **)user_data;
	ViewBindingParser *parser =
		g_hash_table_lookup(*view_binding_parser_map, element_name);
	if (parser && parser->handle_attribute)
		parser->handle_attribute(element_name, attribute_names,
					 attribute_values, &parser->user_data);
}

static void destroy_view_binding_parser(ViewBindingParser *parser)
{
	if (parser->destroy_user_data && parser->user_data)
		parser->destroy_user_data(&parser->user_data);
	parser->user_data = NULL;
}

static void destroy_list_class_id(GList **class_id_list)
{
	g_list_free_full(*class_id_list, (GDestroyNotify)destroy_class_id);
}

static void destroy_class_id(ClassId *class_id)
{
	if (class_id) {
		g_free(class_id->class);
		g_free(class_id->id);
		g_free(class_id);
	}
}

static void destroy_signal_array(GArray **signal_array)
{
	if (signal_array == NULL || *signal_array == NULL)
		return;

	int len = (*signal_array)->len;
	for (int i = 0; i < len; i++) {
		gchar *signal = g_array_index(*signal_array, gchar *, i);
		g_free(signal);
	}
	g_array_free(*signal_array, TRUE);
}

static void handle_object_attribute(const gchar *element_name,
				    const gchar **attribute_names,
				    const gchar **attribute_values,
				    gpointer user_data)
{
	GList **class_id_list = (GList **)user_data;
	const gchar **cursor_name = attribute_names;
	const gchar **cursor_value = attribute_values;

	gchar *class_value = NULL;
	gchar *id_value = NULL;

	while (cursor_name && *cursor_name) {
		if (g_strcmp0(*cursor_name, "class") == 0) {
			class_value = g_strdup(*cursor_value);
		} else if (g_strcmp0(*cursor_name, "id") == 0) {
			id_value = g_strdup(*cursor_value);
		}
		cursor_name++;
		cursor_value++;
	}

	if (class_value && id_value) {
		ClassId *class_id = g_new0(ClassId, 1);
		class_id->class = class_value;
		class_id->id = id_value;
		*class_id_list = g_list_append(*class_id_list, class_id);
	} else {
		if (class_value)
			g_free(class_value);
		if (id_value)
			g_free(id_value);
	}
}

static void handle_signal_attribute(const gchar *element_name,
				    const gchar **attribute_names,
				    const gchar **attribute_values,
				    gpointer user_data)
{
	GArray **signal_array = (GArray **)user_data;
	const gchar **cursor_name = attribute_names;
	const gchar **cursor_value = attribute_values;

	gchar *signal_value = NULL;

	while (cursor_name && *cursor_name) {
		if (g_strcmp0(*cursor_name, "handler") == 0) {
			signal_value = g_strdup(*cursor_value);
			break;
		}
		cursor_name++;
		cursor_value++;
	}

	if (signal_value) {
		if (*signal_array == NULL) {
			*signal_array =
				g_array_new(FALSE, FALSE, sizeof(gchar *));
		}
		g_array_append_val(*signal_array, signal_value);
	}
}

static void generate_code(GHashTable *view_binding_parser_map,
			  const gchar *file_name)
{
	const gchar *dot = g_strrstr(file_name, ".");
	const gsize len = dot - file_name;

	g_autofree gchar *base_name = g_strndup(file_name, len);
	g_autoptr(GString) base_string = g_string_new(base_name);
	g_string_replace(base_string, "-", "_", -1);
	g_string_ascii_down(base_string);

	g_autofree gchar *output_file_name =
		g_strconcat(base_string->str, "_viewbinding.h", NULL);
	g_autofree gchar *output_file_path =
		g_build_filename(output_directory, output_file_name, NULL);
	g_autoptr(GError) error = NULL;

	// check and free output_buffer
	if (output_buffer) {
		g_string_free(output_buffer, TRUE);
		output_buffer = NULL;
	}

	output_buffer = g_string_new(NULL);
	g_string_append_printf(
		output_buffer,
		"/* Generated By View Binding Code Generator, Do Not Edit By Hand */\n\n");

	// header guard
	g_string_append_printf(output_buffer, "#ifndef %s_%s_VIEW_BINDING_H_\n",
			       application_id, base_string->str);
	g_string_append_printf(output_buffer, "#define %s_%s_VIEW_BINDING_H_\n",
			       application_id, base_string->str);
	g_string_append_printf(output_buffer, "\n");

	// add view binding inside utils guard
	g_string_append_printf(output_buffer,
			       "#ifndef VIEW_BINDING_INSIDE_UTILS\n");
	g_string_append_printf(output_buffer,
			       "#define VIEW_BINDING_INSIDE_UTILS\n");
	g_string_append_printf(output_buffer, "\n");
	g_string_append_printf(
		output_buffer,
		"#define view_binding_full(widget_class, WidgetType, BindingType, binding_name, widget_name) \\\n");
	g_string_append_printf(
		output_buffer,
		"\tgtk_widget_class_bind_template_child_full(GTK_WIDGET_CLASS(widget_class), #widget_name, FALSE, G_STRUCT_OFFSET(WidgetType, binding_name) + G_STRUCT_OFFSET(BindingType, widget_name));\n");
	g_string_append_printf(output_buffer, "\n");
	g_string_append_printf(
		output_buffer,
		"#define view_binding_full_private(widget_class, WidgetType, BindingType, binding_name, widget_name) \\\n");
	g_string_append_printf(
		output_buffer,
		"\tgtk_widget_class_bind_template_child_full(GTK_WIDGET_CLASS(widget_class), #widget_name, FALSE, G_PRIVATE_OFFSET(WidgetType, binding_name) + G_STRUCT_OFFSET(BindingType, widget_name));\n");
	g_string_append_printf(output_buffer, "\n");
	g_string_append_printf(output_buffer,
			       "#endif /* VIEW_BINDING_INSIDE_UTILS */\n");

	g_hash_table_foreach(view_binding_parser_map, hash_table_for_each,
			     base_string->str);

	// end header guard
	g_string_append_printf(output_buffer,
			       "\n#endif /* %s_%s_VIEW_BINDING_H_ */\n",
			       application_id, base_string->str);

	g_file_set_contents(output_file_path, output_buffer->str,
			    output_buffer->len, &error);
	if (error) {
		g_printerr("Error writing to file %s: %s\n", output_file_name,
			   error->message);
	}

	g_string_free(output_buffer, TRUE);
	output_buffer = NULL;
}

static void generate_object_code(const gchar *base_name, gpointer user_data)
{
	GList **class_id_list = (GList **)user_data;
	if (class_id_list == NULL || *class_id_list == NULL)
		return;
	guint size = g_list_length(*class_id_list);
	if (size == 0)
		return;

	// change base_name to PascalCase for struct name
	gchar **sp = g_strsplit_set(base_name, "_", -1);
	g_autoptr(GString) name_builder = g_string_new(NULL);
	guint name_size = g_strv_length(sp);
	for (guint i = 0; i < name_size; i++) {
		gchar *part = sp[i];
		if (part && part[0] != '\0') {
			if (g_ascii_islower(part[0])) {
				part[0] = g_ascii_toupper(part[0]);
			}
			g_string_append(name_builder, part);
		}
	}
	g_strfreev(sp);

	g_string_append_printf(output_buffer, "\n/* Class Bindings */\n");
	// generate view binding struct
	g_string_append_printf(output_buffer, "typedef struct {\n");
	for (int i = 0; i < size; i++) {
		ClassId *class_id =
			(ClassId *)g_list_nth_data(*class_id_list, i);
		g_string_append_printf(output_buffer, "\t%s *%s;\n",
				       class_id->class, class_id->id);
	}
	g_string_append_printf(output_buffer, "} %sBinding;\n",
			       name_builder->str);

	// generate view binding macro
	g_string_append_printf(output_buffer, "\n");
	g_string_append_printf(
		output_buffer,
		"#define %s_view_binding(widget_class, WidgetType, binding_name) \\\n",
		base_name);
	g_string_append_printf(output_buffer, "\tdo { \\\n");
	for (int i = 0; i < size; i++) {
		ClassId *class_id =
			(ClassId *)g_list_nth_data(*class_id_list, i);
		g_string_append_printf(
			output_buffer,
			"\t\tview_binding_full(widget_class, WidgetType, %sBinding, binding_name, %s) \\\n",
			name_builder->str, class_id->id);
	}
	g_string_append_printf(output_buffer, "\t} while(0) \n");

	// generate view binding private macro
	g_string_append_printf(output_buffer, "\n");
	g_string_append_printf(
		output_buffer,
		"#define %s_view_binding_private(widget_class, WidgetType, binding_name) \\\n",
		base_name);
	g_string_append_printf(output_buffer, "\tdo { \\\n");
	for (int i = 0; i < size; i++) {
		ClassId *class_id =
			(ClassId *)g_list_nth_data(*class_id_list, i);
		g_string_append_printf(
			output_buffer,
			"\t\tview_binding_full_private(widget_class, WidgetType, %sBinding, binding_name, %s) \\\n",
			name_builder->str, class_id->id);
	}
	g_string_append_printf(output_buffer, "\t} while(0) \n");
}

static void generate_signal_code(const gchar *base_name, gpointer user_data)
{
	GArray **signal_array = (GArray **)user_data;
	if (signal_array == NULL || *signal_array == NULL)
		return;

	const int size = (*signal_array)->len;
	if (size == 0)
		return;
	g_string_append_printf(output_buffer, "\n/* Signal Handlers */\n");
	g_string_append_printf(
		output_buffer,
		"#define %s_view_binding_callback(widget_class) \\\n",
		base_name);
	g_string_append_printf(output_buffer, "\tdo { \\\n");
	for (int i = 0; i < size; i++) {
		gchar *signal = g_array_index(*signal_array, gchar *, i);
		g_string_append_printf(
			output_buffer,
			"\t\tgtk_widget_class_bind_template_callback(GTK_WIDGET_CLASS(widget_class), %s); \\\n",
			signal);
	}
	g_string_append_printf(output_buffer, "\t} while(0) \n");
}

static void hash_table_for_each(gpointer key, gpointer value,
				gpointer user_data)
{
	ViewBindingParser *parser = (ViewBindingParser *)value;
	const gchar *base_name = user_data;

	if (parser && parser->generate_code)
		parser->generate_code(base_name, &parser->user_data);
}
