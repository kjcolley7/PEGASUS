DEFAULT_LAYOUT = {
	"default": "@TEXT",
	"segments": [
		{
			"name": "@PEG",
			"prot": "r",
			"vmaddr": 0x0100,
			"header": True,
		},
		{
			"name": "@TEXT",
			"prot": "rx",
			#TODO: support "sections" attribute
		},
		{
			"name": "@CONST",
			"prot": "r",
		},
		{
			"name": "@DATA",
			"prot": "rw",
			"sections": [
				"*",
				".ZEROINIT",
			],
		},
		{
			"name": "@STACK",
			"prot": "rw",
			"vmaddr": 0xFA00,
			"vmsize": 0x400,
		},
		{
			"name": "@SYS",
			"prot": "x",
			"vmaddr": 0xFF00,
			"emit": False,
		}
	],
	"entrypoints": [
		"@start",
	]
}
