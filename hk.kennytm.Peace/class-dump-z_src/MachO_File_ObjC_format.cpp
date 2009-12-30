/*

MachO_File_ObjC_Format.cpp ... Format ObjC types into string.

Copyright (C) 2009  KennyTM~

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "MachO_File_ObjC.h"
#include <string>
#include <cstring>
#include "string_util.h"
#include <pcre.h>
#include <cstdio>
#include <algorithm>
#include "combine_dependencies.h"
#include "or.h"
#include "snprintf.h"

using namespace std;

static void print_banner (FILE* f, const char* selfpath) {
	fprintf(f,
			"/**\n"
			" * This header is generated by class-dump-z 0.2a.\n"
			" * class-dump-z is Copyright (C) 2009 by KennyTM~, licensed under GPLv3.\n"
			" *\n"
			" * Source: %s\n"
			" */\n\n", selfpath);
}

void MachO_File_ObjC::set_class_filter(const char* regexp) {
	if (m_class_filter != NULL) pcre_free(m_class_filter);
	if (m_class_filter_extra != NULL) pcre_free(m_class_filter_extra);
	
	const char* errStr = NULL;
	int erroffset;
	m_class_filter = pcre_compile(regexp, 0, &errStr, &erroffset, NULL);
	if (m_class_filter != NULL)
		m_class_filter_extra = pcre_study(m_class_filter, 0, &errStr);
	if (errStr != NULL)
		fprintf(stderr, "Warning: Encountered error while parsing RegExp pattern '%s' at offset %d: %s.\n", regexp, erroffset, errStr);
}

void MachO_File_ObjC::set_method_filter(const char* regexp) {
	if (m_method_filter != NULL) pcre_free(m_method_filter);
	if (m_method_filter_extra != NULL) pcre_free(m_method_filter_extra);
	
	const char* errStr = NULL;
	int erroffset;
	m_method_filter = pcre_compile(regexp, 0, &errStr, &erroffset, NULL);
	if (m_method_filter != NULL)
		m_method_filter_extra = pcre_study(m_method_filter, 0, &errStr);
	if (errStr != NULL)
		fprintf(stderr, "Warning: Encountered error while parsing RegExp pattern '%s' at offset %d: %s.\n", regexp, erroffset, errStr);
}

bool MachO_File_ObjC::name_killable(const char* name, size_t length, bool check_kill_prefix) const throw() {
	if (m_class_filter != NULL)
		if (0 != pcre_exec(m_class_filter, m_class_filter_extra, name, length, 0, 0, NULL, 0))
			return true;
	if (check_kill_prefix)
		for (vector<string>::const_iterator kit = m_kill_prefix.begin(); kit != m_kill_prefix.end(); ++ kit)
			if (!kit->empty())
				if (strncmp(name+strspn(name, "_"), kit->c_str(), kit->size()) == 0)
					return true;
	return false;
}



struct Method_AlphabeticSorter {
	const vector<MachO_File_ObjC::Method>& v;
	Method_AlphabeticSorter(const vector<MachO_File_ObjC::Method>& v_) : v(v_) {}
	
	bool operator() (unsigned a, unsigned b) const { return strcmp(v[a].raw_name, v[b].raw_name) < 0; }
};

struct Method_AlphabeticAltSorter {
	const vector<MachO_File_ObjC::Method>& v;
	Method_AlphabeticAltSorter(const vector<MachO_File_ObjC::Method>& v_) : v(v_) {}
	
	bool operator() (unsigned a, unsigned b) const {
		const MachO_File_ObjC::Method& ma = v[a], &mb = v[b];
		
		if (ma.is_class_method != mb.is_class_method)
			return ma.is_class_method;
		else {
			bool is_a_init = strncmp(ma.raw_name, "init", 4) == 0 && !(ma.raw_name[4] >= 'a' && ma.raw_name[4] <= 'z');
			bool is_b_init = strncmp(mb.raw_name, "init", 4) == 0 && !(mb.raw_name[4] >= 'a' && mb.raw_name[4] <= 'z');
			if (is_a_init != is_b_init)
				return is_a_init;
			else
				return strcmp(v[a].raw_name, v[b].raw_name) < 0;
		}
	}
};

struct Property_AlphabeticSorter {
	const vector<MachO_File_ObjC::Property>& v;
	Property_AlphabeticSorter(const vector<MachO_File_ObjC::Property>& v_) : v(v_) {}
	
	bool operator() (unsigned a, unsigned b) { return v[a].name < v[b].name; }
};

string MachO_File_ObjC::Property::format(const ObjCTypeRecord& record, const MachO_File_ObjC& self, bool print_method_addresses, int print_comments) const throw() {
	if (hidden != PS_None && print_comments == 0)
		return "";
	
	if (self.m_method_filter != NULL)
		if (0 != pcre_exec(self.m_method_filter, self.m_method_filter_extra, name.c_str(), name.size(), 0, 0, NULL, 0))
			return "";
	
	string res;
	switch (hidden) {
		case PS_AdoptingProtocol: res = "// in a protocol: "; break;
		case PS_Inherited: res = "// inherited: "; break;
		default:
			break;
	}
	
	res += "@property(";
	if (readonly)
		res += "readonly, ";
	if (copy)
		res += "copy";
	else if (retain)
		res += "retain";
	else
		res += "assign";
	if (nonatomic)
		res += ", nonatomic";
	if (has_getter) {
		res += ", getter=";
		res += getter;
	}
	if (has_setter) {
		res += ", setter=";
		res += setter;
	}
	res += ") ";
	if (gc_strength == GC_Strong)
		res += "__strong ";
	else if (gc_strength == GC_Weak)
		res += "__weak ";
	res += record.format(type, name);
	res.push_back(';');
	
	bool printed_double_slash = false;
	if (print_method_addresses) {
		if (getter_vm_address != 0 || setter_vm_address != 0) {
			printed_double_slash = true;
			res += "\t// ";
			if (getter_vm_address != 0) res += numeric_format("G=0x%x; ", getter_vm_address);
			if (setter_vm_address != 0) res += numeric_format("S=0x%x; ", setter_vm_address);
		}
	}
	if (print_comments >= 2) {
		if (impl_method != IM_None) {
			if (!printed_double_slash)
				res += "\t// ";
			switch (impl_method) {
				case IM_Synthesized:
					res += "@synthesize";
					if (name != synthesized_to) {
						res.push_back('=');
						res += synthesized_to;
					}
					break;
				case IM_Dynamic:
					res += "@dynamic";
					break;
				case IM_Converted:
					res += "converted property";
					break;
				default:
					break;
			}
		}
	}
	
	res.push_back('\n');
	
	return res;
}

std::string MachO_File_ObjC::format_type_with_hints(const ObjCTypeRecord& record, const std::string& reconstructed_raw_name, const ReducedMethod& method, int index) const {
	if (m_hints_file != NULL) {
		TSVFile::RowID row = m_hints_file->find_row_for_table(m_hints_method_table, reconstructed_raw_name);
		if (row != TSVFile::invalid_row) {
			const std::vector<std::string>& row_content = m_hints_file->get_row_for_table(m_hints_method_table, row);
			if (index == 0)
				++ index;
			else
				-- index;
			return row_content[index];
		}
	}
	return record.format(method.types[index], "");
}

string MachO_File_ObjC::Method::format(const ObjCTypeRecord& record, const MachO_File_ObjC& self, bool print_method_addresses, int print_comments, const ClassType& cls) const throw() {
	if (propertize_status != PS_None && (print_comments == 0 || (print_comments == 1 && propertize_status != PS_AdoptingProtocol && propertize_status != PS_Inherited)))
		return "";
	
	if (self.m_method_filter != NULL)
		if (0 != pcre_exec(self.m_method_filter, self.m_method_filter_extra, raw_name, strlen(raw_name), 0, 0, NULL, 0))
			return "";
	
	string res;
	switch (propertize_status) {
		case PS_DeclaredGetter: res = "// declared property getter: "; break;
		case PS_DeclaredSetter: res = "// declared property setter: "; break;
		case PS_ConvertedGetter: res = "// converted property getter: "; break;
		case PS_ConvertedSetter: res = "// converted property setter: "; break;
		case PS_AdoptingProtocol: res = "// in a protocol: "; break;
		case PS_Inherited: res = "// inherited: "; break;
		default:
			break;
	}
	
	string rrname = MachO_File_ObjC::reconstruct_raw_name(cls, *this);
	
	if (is_class_method)
		res.push_back('+');
	else
		res.push_back('-');
	if (self.m_has_whitespace)
		res.push_back(' ');
	res.push_back('(');
	
	res += self.format_type_with_hints(record, rrname, *this, 0);
	res.push_back(')');
	
	if (components.size() == 3) {
		res += raw_name;
		if (res[res.size()-1] == ']')
			res.erase(res.size()-1);
	} else {
		for (unsigned i = 3; i < components.size(); ++ i) {
			if (i != 3)
				res.push_back(' ');
			res += components[i];
			res += ":(";
			res += self.format_type_with_hints(record, rrname, *this, i);
			res.push_back(')');
			res += argname[i];
		}
	}
	res.push_back(';');
	
	if (print_method_addresses && vm_address != 0)
		res += numeric_format("\t// 0x%x", vm_address);
	
	res.push_back('\n');
	
	return res;
}

string MachO_File_ObjC::ClassType::format(const ObjCTypeRecord& record, const MachO_File_ObjC& self, bool print_method_addresses, int print_comments, bool print_ivar_offsets, MachO_File_ObjC::SortBy sort_by, bool show_only_exported_classes) const throw() {
	if ((self.m_hide_cats && type == CT_Category) || (self.m_hide_dogs && type == CT_Protocol))
		return "";
	
	if (self.name_killable(name, strlen(name), type != CT_Category)) {
		if (type != CT_Category || self.name_killable(superclass_name, strlen(superclass_name), false))
			return "";
	}
	
	bool all_methods_filtered = true;
	
	string res;
	
	if (attributes & RO_HIDDEN) {
		if (show_only_exported_classes)
			return "";
		if (attributes & RO_EXCEPTION)
			res += "__attribute__((visibility(\"hidden\"),objc_exception))\n";
		else
			res += "__attribute__((visibility(\"hidden\")))\n";
	} else if (attributes & RO_EXCEPTION) {
		res += "__attribute__((objc_exception))\n";
	}
	
	vector<unsigned> property_index_remap (properties.size());
	for (unsigned i = 0; i < property_index_remap.size(); ++ i)
		property_index_remap[i] = i;
	
	vector<unsigned> method_index_remap (methods.size());
	for (unsigned i = 0; i < method_index_remap.size(); ++ i)
		method_index_remap[i] = i;
	
	if (sort_by != SB_None) {
		sort(property_index_remap.begin(), property_index_remap.end(), Property_AlphabeticSorter(properties));
		if (sort_by == SB_Alphabetic)
			sort(method_index_remap.begin(), method_index_remap.end(), Method_AlphabeticSorter(methods));
		else if (sort_by == SB_AlphabeticAlt)
			sort(method_index_remap.begin(), method_index_remap.end(), Method_AlphabeticAltSorter(methods));
	}
	
	switch (type) {
		case CT_Class:
			res += "@interface ";
			res += name;
			if (superclass_name != NULL) {
				res += " : ";
				res += superclass_name;
			}
			break;
			
		case CT_Protocol:
			res += "@protocol ";
			res += name;
			break;
			
		case CT_Category:
			res += "@interface ";
			res += superclass_name;
			res += " (";
			res += name;
			res.push_back(')');
			break;
			
		default:
			res += numeric_format("@wtf_type%u ", type);
			break;
	}
	
	if (adopted_protocols.size() > 0) {
		res += " <";
		bool is_first = true;
		for (vector<unsigned>::const_iterator cit = adopted_protocols.begin(); cit != adopted_protocols.end(); ++ cit) {
			if (is_first)
				is_first = false;
			else
				res += ", ";
			res += self.ma_classes[*cit].name;
		}
		
		res.push_back('>');
	}
	
	if (type == CT_Class && self.m_method_filter == NULL) {
		res += " {\n";
		bool is_private = false;
		for (vector<Ivar>::const_iterator cit = ivars.begin(); cit != ivars.end(); ++ cit) {
			if (is_private != cit->is_private) {
				if (cit->is_private)
					res += "@private\n";
				else
					res += "@protected\n";
				is_private = cit->is_private;
			}
			res += record.format(cit->type, cit->name, 1);
			res.push_back(';');
			if (print_ivar_offsets) {
				char offset_string[32];
				snprintf(offset_string, 32, "\t// %u = 0x%x", cit->offset, cit->offset);
				res += offset_string;
			}
			res.push_back('\n');
		}
		res.push_back('}');
	}
	
	res.push_back('\n');
	
	bool is_optional = false;
	for (vector<unsigned>::const_iterator cit = property_index_remap.begin(); cit != property_index_remap.end(); ++ cit) {
		const Property& property = properties[*cit];
		string formatted_property = property.format(record, self, print_method_addresses, print_comments);
		if (!formatted_property.empty()) {
			if (property.optional != is_optional) {
				is_optional = property.optional;
				res += is_optional ? "@optional\n" : "@required\n";
			}
			all_methods_filtered = false;
			res += formatted_property;
		}
	}
	
	for (vector<unsigned>::const_iterator cit = method_index_remap.begin(); cit != method_index_remap.end(); ++ cit) {
		const Method& method = methods[*cit];
		string formatted_method = method.format(record, self, print_method_addresses, print_comments, *this);
		if (!formatted_method.empty()) {
			if (method.optional != is_optional) {
				is_optional = method.optional;
				res += is_optional ? "@optional\n" : "@required\n";
			}
			all_methods_filtered = false;
			res += formatted_method;
		}
	}
	
	if (all_methods_filtered && self.m_method_filter != NULL)
		return "";
	
	res += "@end\n\n";
	
	return res;
}

#pragma mark -

bool mfoc_AlphabeticSorter(const MachO_File_ObjC::ClassType* a, const MachO_File_ObjC::ClassType* b) throw() {
	if (a->type == MachO_File_ObjC::ClassType::CT_Protocol && b->type != MachO_File_ObjC::ClassType::CT_Protocol)
		return true;
	else if (a->type != MachO_File_ObjC::ClassType::CT_Protocol && b->type == MachO_File_ObjC::ClassType::CT_Protocol)
		return false;
	
	const char* a_major = a->type == MachO_File_ObjC::ClassType::CT_Category ? a->superclass_name : a->name;
	const char* b_major = b->type == MachO_File_ObjC::ClassType::CT_Category ? b->superclass_name : b->name;
	const char* a_minor = a->type == MachO_File_ObjC::ClassType::CT_Category ? a->name : NULL;
	const char* b_minor = b->type == MachO_File_ObjC::ClassType::CT_Category ? b->name : NULL;
	
	int res = strcmp(a_major, b_major);
	if (res < 0)
		return true;
	else if (res > 0)
		return false;
	
	if (a_minor == NULL)
		return true;
	else if (b_minor == NULL)
		return false;
	
	res = strcmp(a_minor, b_minor);
	if (res < 0)
		return true;
	else
		return false;
}

void MachO_File_ObjC::print_class_type(SortBy sort_by, bool print_method_addresses, int print_comments, bool print_ivar_offsets, SortBy sort_methods_by, bool show_only_exported_classes) const throw() {	
	switch (sort_by) {
		default:
			for (vector<ClassType>::const_iterator cit = ma_classes.begin(); cit != ma_classes.end(); ++ cit)
				printf("%s", cit->format(m_record, *this, print_method_addresses, print_comments, print_ivar_offsets, sort_methods_by, show_only_exported_classes).c_str());
			break;
		case SB_Alphabetic: {
			vector<const ClassType*> remap;
			remap.reserve(ma_classes.size());
			for (vector<ClassType>::const_iterator cit = ma_classes.begin(); cit != ma_classes.end(); ++ cit)
				remap.push_back(&*cit);
			sort(remap.begin(), remap.end(), mfoc_AlphabeticSorter);
			
			for (vector<const ClassType*>::const_iterator cit = remap.begin(); cit != remap.end(); ++ cit)
				printf("%s", (*cit)->format(m_record, *this, print_method_addresses, print_comments, print_ivar_offsets, sort_methods_by, show_only_exported_classes).c_str());
			break;
		}
	}
}

void MachO_File_ObjC::print_struct_declaration(SortBy sort_by) const throw() {
	print_banner(stdout, self_path());
	
	vector<ObjCTypeRecord::TypeIndex> public_struct_types = m_record.all_public_struct_types();
	if (sort_by == SB_Alphabetic)
		m_record.sort_alphabetically(public_struct_types);
	
	for (vector<ObjCTypeRecord::TypeIndex>::const_iterator cit = public_struct_types.begin(); cit != public_struct_types.end(); ++ cit) {
		const string& name = m_record.name_of_type(*cit);
		if (!name_killable(name.c_str(), name.size(), true))		
			printf("%s;\n\n", m_record.format(*cit, "", 0, true).c_str());
	}
}

namespace {
	struct Header {
		bool include_common;
		string struct_declarations;
		string declaration;
		tr1::unordered_map<ObjCTypeRecord::TypeIndex, ObjCTypeRecord::EdgeStrength> dependencies;
	};
}

void MachO_File_ObjC::write_header_files(const char* filename, bool print_method_addresses, int print_comments, bool print_ivar_offsets, SortBy sort_by, bool show_only_exported_classes) const throw() {
	vector<ObjCTypeRecord::TypeIndex> public_struct_types = m_record.all_public_struct_types();
	tr1::unordered_map<string, Header> headers;
	
	const char* cached_self_path = self_path();
	const char* self_filename = OR(cached_self_path, filename);
	const char* last_component = OR(strrchr(self_filename, '/'), self_filename);
	if (*last_component == '/') ++ last_component;
	const char* dot_position = strrchr(last_component, '.');
	string aggr_filename = dot_position == NULL ? last_component : string(last_component, dot_position);	
	
	// Filter out those structs not matching the regexp or having specified prefix.
	bool need_killer_check = m_class_filter != NULL || !m_kill_prefix.empty();
	if (need_killer_check) {
		for (int i = public_struct_types.size()-1; i >= 0; -- i) {
			ObjCTypeRecord::TypeIndex idx = public_struct_types[i];
			const string& name = m_record.name_of_type(idx);
			if (name_killable(name.c_str(), name.size(), true))
				public_struct_types.erase(public_struct_types.begin() + i);
		}
	}
	
	// Distribute each class into files. 
	for (vector<ClassType>::const_iterator cit = ma_classes.begin(); cit != ma_classes.end(); ++ cit) {
		Header h;
		h.declaration = cit->format(m_record, *this, print_method_addresses, print_comments, print_ivar_offsets, sort_by, show_only_exported_classes);
		// we still need to pay lip service to create an empty file for the filtered types if someone else it going to include us.
		if (!h.declaration.empty() || m_record.link_count(cit->type_index, true) > 0) {
			const tr1::unordered_map<ObjCTypeRecord::TypeIndex, ObjCTypeRecord::EdgeStrength>* dep = m_record.dependencies(cit->type_index);
			if (dep != NULL)
				h.dependencies = *dep;
			string file_name = cit->type == ClassType::CT_Category ? cit->superclass_name : cit->name;
			if (file_name == aggr_filename)
				file_name += "-Class";
			pair<tr1::unordered_map<string, Header>::iterator, bool> res = headers.insert(pair<string, Header>(file_name, h));
			if (!res.second) {
				res.first->second.declaration += h.declaration;
				combine_dependencies(res.first->second.dependencies, h.dependencies);
			}
		}
	}
	
	// TODO: pull out all structs which k_in = 1 into the header file.
	
	// Write the aggregation file first.
	
	FILE* f_aggr = fopen((aggr_filename + ".h").c_str(), "wt");
	print_banner(f_aggr, cached_self_path);
	fprintf(f_aggr, "#import \"%s-Structs.h\"\n", aggr_filename.c_str());
	for (tr1::unordered_map<string, Header>::const_iterator hit = headers.begin(); hit != headers.end(); ++ hit) {
		fprintf(f_aggr, "#import \"%s.h\"\n", hit->first.c_str());
	}
	fclose(f_aggr);
	
	// Print the structs.
	FILE* f_structs = fopen((aggr_filename + "-Structs.h").c_str(), "wt");
	print_banner(f_structs, cached_self_path);
	fprintf(f_structs, "%s\n", m_record.format_structs_with_forward_declarations(public_struct_types).c_str());
	fclose(f_structs);
	
	// Now print to each file.
	vector<ObjCTypeRecord::TypeIndex> weak_dependencies;
	tr1::unordered_set<string> already_included;

	for (tr1::unordered_map<string, Header>::const_iterator hit = headers.begin(); hit != headers.end(); ++ hit) {
		string cur_filename = hit->first;
		cur_filename += ".h";
		FILE* f = fopen(cur_filename.c_str(), "wt");
		print_banner(f, cached_self_path);
		
		bool include_structs = true;
		weak_dependencies.clear();
		already_included.clear();
		// Write imports.
		for (tr1::unordered_map<ObjCTypeRecord::TypeIndex, ObjCTypeRecord::EdgeStrength>::const_iterator dit = hit->second.dependencies.begin(); dit != hit->second.dependencies.end(); ++ dit) {
			if (m_record.is_struct_type(dit->first)) {
				if (include_structs) {
					include_structs = false;
					fprintf(f, "#import \"%s-Structs.h\"\n", aggr_filename.c_str());
				}
			} else if (dit->second == ObjCTypeRecord::ES_Strong) {
				if (m_record.is_external_type(dit->first)) {
					tr1::unordered_map<ObjCTypeRecord::TypeIndex, string>::const_iterator lib_it = ma_include_paths.find(dit->first);
					if (lib_it != ma_include_paths.end()) {
						string lib_inc_path = lib_it->second;
						if (lib_inc_path[lib_inc_path.size()-1] == '/') {
							lib_inc_path += m_record.name_of_type(dit->first);
							lib_inc_path += ".h";
						}
						if (already_included.find(lib_inc_path) == already_included.end()) {
							fprintf(f, "#import <%s>\n", lib_inc_path.c_str());
							already_included.insert(lib_inc_path);
						}
					} else
						fprintf(f, "#import <%s.h> // Unknown library\n", m_record.name_of_type(dit->first).c_str());
				} else
					fprintf(f, "#import \"%s.h\"\n", m_record.name_of_type(dit->first).c_str());
			} else if (dit->second == ObjCTypeRecord::ES_Weak)
				weak_dependencies.push_back(dit->first);
		}
		
		fprintf(f, "\n%s", m_record.format_forward_declaration(weak_dependencies).c_str());
		fprintf(f, "\n%s", hit->second.declaration.c_str());
		fclose(f);
	}
}

std::string MachO_File_ObjC::reconstruct_raw_name(const ClassType& cls, const ReducedMethod& method) {
	std::string reconstructed_raw_name = method.is_class_method ? "+[" : "-[";
	
	if (cls.type == ClassType::CT_Protocol) {
		reconstructed_raw_name += "id<";
		reconstructed_raw_name += cls.name;
		reconstructed_raw_name += ">";
	} else if (cls.type == ClassType::CT_Category)
		reconstructed_raw_name += cls.superclass_name;
	else 
		reconstructed_raw_name += cls.name;

	reconstructed_raw_name.push_back(' ');
	reconstructed_raw_name += method.raw_name;
	if (reconstructed_raw_name[reconstructed_raw_name.size()-1] != ']')
		reconstructed_raw_name.push_back(']');
	
	return reconstructed_raw_name;
}

/*
 
 if (has_raw_name && m_hints_file) {
 // try to read types from hints file.
 TSVFile::RowID row = m_hints_file->add_row_for_table(m_hints_method_table, reconstruct_raw_name(cls, method));
 vector<string>& hinted_types = m_hints_file->get_row_for_table(m_hints_method_table, row);
 if (hinted_types.size() + 1 != method.types.size()) {
 hinted_types.resize(1);
 hinted_types.push_back(m_record.format(method.types.front(), ""));
 for (size_t j = 3; j < method.types.size(); ++ j)
 hinted_types.push_back(m_record.format(method.types[j], ""));
 }
 size_t j_hints = 1, j_types = 0;
 for (; j_hints < hinted_types.size(); ++ j_hints) {
 // build weak link on ID reference.
 if (m_record.is_id_type(method.types[j_types]) && hinted_types[j_hints] != "id") {
 // OMG<WTF, BBQ>* --> @"OMG<WTF,BBQ>"
 // id<WTF, BBQ> --> @"<WTF,BBQ>"
 // OMG* --> @"OMG"
 
 string s = hinted_types[j_hints];
 // strip spaces.
 for (size_t k = s.size(); k != 0; --k) {
 if (s[k-1] == '$' || s[k-1] == '_' || s[k-1] == '<' || s[k-1] == '>' || s[k-1] == ',')
 continue;
 if (s[k-1] >= '0' && s[k-1] <= '9')
 continue;
 if (s[k-1] >= 'a' && s[k-1] <= 'z')
 continue;
 if (s[k-1] >= 'A' && s[k-1] <= 'Z')
 continue;
 s.erase(k-1);
 }
 
 if (s.substr(0, 2) == "id")
 s.erase(0, 2);
 if (!s.empty()) {
 s.insert(0, "@\"");
 s.push_back('"');
 m_record.add_strong_link(cls.type_index, m_record.parse(s, false));
 }
 }
 }
 }
*/ 
 

void MachO_File_ObjC::set_hints_file(const char* filename) {
	delete m_hints_file;
	m_hints_file = filename ? new TSVFile(filename) : NULL;
	if (m_hints_file) {
		bool already_exists = false;
		m_hints_method_table = m_hints_file->add_table("methods", &already_exists);
		if (!already_exists) {
			m_hints_file->add_table_comment(m_hints_method_table, "This section contains customized type signature for Objective-C methods.");
			m_hints_file->add_table_comment(m_hints_method_table, "You can replace \"id\" with more specific type to improve the headers.");
			m_hints_file->add_table_comment(m_hints_method_table, "");
			m_hints_file->add_table_comment(m_hints_method_table, "Method\tAttributes\tReturn type\tArg2\tArg3\t...");
		}
		
		for (vector<ClassType>::const_iterator cit = ma_classes.begin(); cit != ma_classes.end(); ++ cit) {
			for (vector<Method>::const_iterator mit = cit->methods.begin(); mit != cit->methods.end(); ++ mit) {
				TSVFile::RowID row = m_hints_file->add_row_for_table(m_hints_method_table, reconstruct_raw_name(*cit, *mit));
				vector<string>& hinted_types = m_hints_file->get_row_for_table(m_hints_method_table, row);
				if (hinted_types.size() + 1 < mit->types.size()) {
					hinted_types.resize(1);
					hinted_types.push_back(m_record.format(mit->types.front(), ""));
					for (size_t j = 3; j < mit->types.size(); ++ j)
						hinted_types.push_back(m_record.format(mit->types[j], ""));
				}
				for (size_t j_hints = 1, j_types = 0; j_types < mit->types.size(); ++ j_types, ++ j_hints) {
					// build weak link on ID reference.
					if (m_record.is_id_type(mit->types[j_types]) && hinted_types[j_hints] != "id") {
						// OMG<WTF, BBQ>* --> @"OMG<WTF,BBQ>"
						// id<WTF, BBQ> --> @"<WTF,BBQ>"
						// OMG* --> @"OMG"
						
						string s = hinted_types[j_hints];
						// strip spaces.
						for (size_t k = s.size(); k != 0; --k) {
							if (s[k-1] == '$' || s[k-1] == '_') continue;
							if (s[k-1] == '<' || s[k-1] == '>' || s[k-1] == ',') continue;
							if (s[k-1] >= '0' && s[k-1] <= '9') continue;
							if (s[k-1] >= 'a' && s[k-1] <= 'z') continue;
							if (s[k-1] >= 'A' && s[k-1] <= 'Z') continue;
							s.erase(k-1);
						}
						
						if (s.substr(0, 2) == "id")
							s.erase(0, 2);
						if (!s.empty()) {
							s.insert(0, "@\"");
							s.push_back('"');
							m_record.add_strong_link(cit->type_index, m_record.parse(s, false));
						}
					}
					if (j_types == 0)
						j_types = 2;
				}
			}
		}
	}
}

void MachO_File_ObjC::write_hints_file(const char* filename) const {
	if (m_hints_file && filename)
		m_hints_file->write(filename);
}
