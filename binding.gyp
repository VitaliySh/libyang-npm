{
	'targets': [{
		'target_name': 'libyang',
		'sources': [
		'src/yang_types.c',
		'src/printer_json.c',
		'src/printer_info.c',
		'src/printer_tree.c',
		'src/printer_xml.c',
		'src/printer_yin.c',
		'src/printer_yang.c',
		'src/xpath.c',
		'src/printer.c',
		'src/tree_data.c',
		'src/tree_schema.c',
		'src/parser_yang.c',
		'src/parser_yang_lex.c',
		'src/parser_yang_bis.c',
		'src/parser_json.c',
		'src/parser_xml.c',
		'src/parser_yin.c',
		'src/parser.c',
		'src/xml.c',
		'src/validation.c',
		'src/resolve.c',
		'src/dict.c',
		'src/log.c',
		'src/context.c',
		'src/common.c',

		'src/libyang_javascriptJAVASCRIPT_wrap.cxx' ],
		'include_dirs': [
		'usr/lib64/libpcre.so',

                      ],
		'libraries': [
			"-lpcre"
		],
      	      'variables': {
		"arch%": "<!(node -e 'console.log(process.arch)')"
		},
		'cflags_cc!': [ '-fno-rtti', '-fno-exceptions' ],
		'cflags!': [ '-fno-exceptions' ],
		'conditions' : [
			[ 'arch=="x64"',
				{ 'defines' : [ 'X86PLAT=ON' ], },
			],
			[ 'arch=="ia32"',
				{ 'defines' : [ 'X86PLAT=ON'], },
			],
			[ 'arch=="arm"',
				{ 'defines' : [ 'ARMPLAT=ON'], },
			],
			],
		'defines' : [ 'SWIG',
			'SWIGJAVASCRIPT',
			'BUILDING_NODE_EXTENSION=1',
		]
	}]
}
