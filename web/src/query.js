/*
 * Pharmit Web Client
 * Copyright 2015 David R Koes and University of Pittsburgh
 *  The JavaScript code in this page is free software: you can
    redistribute it and/or modify it under the terms of the GNU
    General Public License (GNU GPL) as published by the Free Software
    Foundation, either version 2 of the License, or (at your option)
    any later version.  The code is distributed WITHOUT ANY WARRANTY;
    without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE.  See the GNU GPL for more details.
 */
/*
	viewer.js
	Object responsible for maintaining molecular viewer.
	Query and Results both call have references to the viewer to set data.
	Query provides a callback for when pharmacophores are selected
*/

var Pharmit = Pharmit || {};

Pharmit.Query = (function() {
	
	function Query(element) {
		//private variables and functions
		var querydiv = $('<div>').addClass('pharmit_query');
		var features = null;
		var ec = $3Dmol.elementColors;
		var colorStyles =  [//each element has name and color (3dmol)
		                    {name: "None", color: null},
		                    {name: "RasMol", color: ec.rasmol},
		                    {name: "White", color: ec.whiteCarbon},
		                    {name: "Green", color: ec.greenCarbon},
		                    {name: "Cyan", color: ec.cyanCarbon},
		                    {name: "Magenta", color: ec.magentaCarbon},
		                    {name: "Yellow", color: ec.yellowCarbon},
		                    {name: "Orange", color: ec.orangeCarbon},
		                    {name: "Purple", color: ec.purpleCarbon},
		                    {name: "Blue", color: ec.blueCarbon}
		                    ];
		
		
		var doSearch = function() {
			
		};
		
		var loadFeatures = function() {
			
		};
		
		var loadReceptor = function() {
			
		};
		
		var addFeature = function() {
			
		};
		
		var loadSession = function() {
			
		};
		
		var saveSession = function() {
			
		};
		
		
		//create jquery selection object for picking molecule style
		var createStyleSelector = function(name, defaultval, callback) {
			var ret = $('<div>');
			var id = name+"MolStyleSelect";
			$('<label for="'+id+'">'+name+' Style</label>').appendTo(ret).addClass('stylelabel');
			
			var select = $('<select name="'+id+'" id="'+id+'">').appendTo(ret).addClass('styleselector');
			for(var i = 0, n = colorStyles.length; i < n; i++) {
				$('<option value="'+i+'">'+colorStyles[i].name+'</option>').appendTo(select);
			}
			
			select.val(defaultval);
			select.selectmenu({width: 120});

			return ret;
		};
		
		//create a split button from a list of vendors and prepend it to header
		var createSearchButton = function(header,vendors) {
			var buttons = $('<div>').addClass('searchdiv');
			var run = $('<button>Search '+vendors[0]+'</button>').appendTo(buttons).button();
			var select = $('<button>Select subset to search</button>').appendTo(buttons).button({text: false, icons: {primary: "ui-icon-triangle-1-s"}});
			
			buttons.buttonset();
			var ul = $('<ul>').appendTo(buttons);
			var lis = [];
			for(var i = 0, n = vendors.length; i < n; i++) {
				lis[i] = '<li>'+vendors[i]+'</li>';
			}
			ul.append(lis);
			ul.hide().menu().on('menuselect', function(event, ui) {
				run.button("option",'label',"Search "+ui.item.text());
			});
			
			//handlers
			run.click(doSearch);
			select.click(
					function() {
						var menu = ul.show().position({
							my: "left top",
							at: "left buttom",
							of: this
						});
						$(document).one('click', function() { menu.hide(); });
						return false;
					});
			
			header.prepend(buttons);
		};
		
		
		//public variables and functions
		
		//called when user picks a pharmacophore
		this.pickCallback = function(pharm) {
			
		};
		
		
		//initialization code
		querydiv.resizable({handles: "e"});
		var header = $('<div>').appendTo(querydiv).addClass("queryheader");
		createSearchButton(header,['MolPort','ZINC']);
		
		//load features and load receptor
		var loaders = $('<div>').appendTo(header);
		var loadfeatures = $('<button>Load Features...</button>').appendTo(loaders).button().click(loadFeatures);
		var loadrec = $('<input type="button">Load Receptor...</input>').appendTo(loaders).button().click(loadReceptor);
		$('<input type="file"').fileinput(loadrec);
		
		//query features
		var body = $('<div>').appendTo(querydiv).addClass("querybody");
		var featuregroup = $('<div>').appendTo(body);
		$('<div>Pharmacophore</div>').appendTo(featuregroup).addClass('queryheading');
		features = $('<div>').appendTo(featuregroup);
		features.accordion({animate: true, collapsible: true,heightStyle:'content'});
		
		var addbutton = $('<button>Add</button>').appendTo(featuregroup).button({text: true, icons: {secondary: "ui-icon-circle-plus"}}).click(addFeature);
		
		//filters
		var filtergroup = $('<div>').appendTo(body);
		$('<div>Filters</div>').appendTo(filtergroup).addClass('queryheading');
		var filters = $('<div>').appendTo(filtergroup);		
		
		var heading = $('<h3>Hit Reduction<br></h3>').appendTo(filters);
		var hitreductionsummary = $('<span class="headingsummary"></span>').appendTo(heading);

		var hitreductions = $('<div class="hitreduction"></div>').appendTo(filters);
		
		var row = $('<div>').addClass('filterrow').appendTo(hitreductions);
		row.append('<label title="Maximum number of orientations returned for each conformation" value="1" for="reduceorienttext">Max Hits per Conf:</label>');
		var maxorient = $('<input id="reduceorienttext" name="max-orient">').appendTo(row).spinner();
		
		row = $('<div>').addClass('filterrow').appendTo(hitreductions);
		row.append('<label title="Maximum number of conformations returned for each compound" value="1" for="reduceconfstext">Max Hits per Mol:</label>');
		var maxconfs = $('<input id="reduceconfstext" name="reduceConfs">').appendTo(row).spinner();
		
		row = $('<div>').addClass('filterrow').appendTo(hitreductions);
		row.append('<label title="Maximum number of hits returned" value="1" for="reducehitstext">Max Total Hits:</label>');
		var maxhits = $('<input id="reducehitstext" name="max-hits">').appendTo(row).spinner();
		
		
		heading = $('<h3>Hit Screening<br></h3>').appendTo(filters);
		var hitscreeningsummary = $('<span class="headingsummary"></span>').appendTo(heading);
		var hitscreening = $('<div class="hitscreening"></div>').appendTo(filters);
		row = $('<div>').addClass('filterrow').appendTo(hitscreening);
		var minweight = $('<input id="minmolweight" name="minMolWeight">').appendTo(row).spinner();
		row.append($('<label title="Minimum/maximum molecular weight (weights are approximate)" value="1" for="maxmolweight">&le;  Molecular Weight &le;</label>'));
		var maxweight = $('<input id="maxmolweight" name=maxMolWeight>').appendTo(row).spinner();

		row = $('<div>').addClass('filterrow').appendTo(hitscreening);
		var minnrot = $('<input id="minnrot" name="minrotbonds">').appendTo(row).spinner();
		row.append($('<label title="Minimum/maximum number of rotatable bonds" value="1" for="maxnrot"> &le;  Rotatable Bonds &le;</label>'));
		var maxnrot = $('<input id="maxnrot" name="maxrotbonds">').appendTo(row).spinner();

		filters.accordion({animate: true, collapsible: true,heightStyle:'content'});
		
		
		//viewer settings
		var vizgroup = $('<div>').appendTo(body);
		$('<div>Visualization</div>').appendTo(vizgroup).addClass('queryheading');
		
		createStyleSelector("Ligand",1, null).appendTo(vizgroup);
		createStyleSelector("Results",1, null).appendTo(vizgroup);
		createStyleSelector("Receptor",2, null).appendTo(vizgroup);
		
		//surface
		
		//load/save session
		var footer = $('<div>').appendTo(querydiv).addClass("queryfooter");
		var loadsession = $('<button>Load Session...</button>').appendTo(footer).button().click(loadSession);
		var savesession = $('<button>Save Session...</button>').appendTo(footer).button().click(saveSession);
		
		
		element.append(querydiv);
	}

	return Query;
})();