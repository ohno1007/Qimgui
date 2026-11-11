// Sample values mirror jni/src/ui/md3/md3_showcase.cpp.
const p = (name, html) => `<div class="probe" data-probe="${name}">${html}</div>`;
const h = text => `<h2>${text}</h2>`;
const row = html => `<div class="row">${html}</div>`;
const icon = (slot='', text='+') => `<md-icon ${slot ? `slot="${slot}"` : ''}>${text}</md-icon>`;
const buttons = disabled => ['filled', 'filled-tonal', 'outlined', 'text', 'elevated'].map(v=>`<md-${v}-button ${disabled?'disabled':''}>Button</md-${v}-button>`).join('');
const chips = disabled => ['assist','filter','input','suggestion'].map(v=>`<md-${v}-chip label="${v[0].toUpperCase()+v.slice(1)}" ${v==='filter'?'selected':''} ${disabled?'disabled':''}></md-${v}-chip>`).join('');
const control = (html,label) => `<label class="control-label">${html}<span>${label}</span></label>`;
const controls = disabled => disabled
 ? control('<md-checkbox checked disabled aria-label="Checkbox"></md-checkbox>','Checkbox')+control('<md-radio disabled name="radio-disabled" aria-label="Radio"></md-radio>','Radio')+control('<md-switch icons selected disabled aria-label="Switch"></md-switch>','Switch')
 : control('<md-checkbox checked aria-label="Checkbox"></md-checkbox>','Checkbox')+control('<md-checkbox indeterminate aria-label="Mixed"></md-checkbox>','Mixed')+control('<md-radio checked name="radio" aria-label="Radio"></md-radio>','Radio')+control('<md-switch icons selected aria-label="Switch"></md-switch>','Switch');
const slider = (label,attrs='',value='40') => `<div class="slider-field"><div class="slider-label"><span>${label}</span><span>${value}</span></div><md-slider min="0" max="100" value="40" aria-label="${label}" ${attrs}></md-slider></div>`;
const field = (variant,label='Label',attrs='') => `<md-${variant}-text-field label="${label}" value="Input text" supporting-text="Supporting text" ${attrs}></md-${variant}-text-field>`;
const tab = variant => `<md-tabs>${['Overview','Activity','Settings'].map((v,i)=>`<md-${variant}-tab ${i===0?'active':''}>${v}</md-${variant}-tab>`).join('')}</md-tabs>`;
const select = variant => `<md-${variant}-select label="Fruit" supporting-text="Choose a fruit">${['Apple','Banana','Cherry'].map((v,i)=>`<md-select-option value="${v.toLowerCase()}" ${i===0?'selected':''}><div slot="headline">${v}</div></md-select-option>`).join('')}</md-${variant}-select>`;
const segments = multiple => `<md-outlined-segmented-button-set ${multiple?'multiselect':''}>${['Day','Week','Month'].map((v,i)=>`<md-outlined-segmented-button label="${v}" ${i==0||(multiple&&i==1)?'selected':''}></md-outlined-segmented-button>`).join('')}</md-outlined-segmented-button-set>`;
const groups = {
 dialog: h('Basic dialog — modal') + '<md-dialog open quick id="reference-dialog"><div slot="headline">Save changes?</div><div slot="content">Your changes will be saved to this project.</div><div slot="actions"><md-text-button>Cancel</md-text-button><md-filled-button>Save</md-filled-button></div></md-dialog>',
 actions: h('Buttons — filled / tonal / outlined / text / elevated')+row('<span class="state">Enabled</span>'+p('buttons',buttons(false)))+row('<span class="state">Disabled</span>'+p('buttons-disabled',buttons(true)))+
 h('Icon buttons — standard / filled / tonal / outlined / disabled filled')+p('icon-buttons',['icon-button','filled-icon-button','filled-tonal-icon-button','outlined-icon-button'].map((v,i)=>`<md-${v} toggle ${i===1||i===2?'selected':''} aria-label="Add">${icon()}</md-${v}>`).join('')+`<md-filled-icon-button disabled aria-label="Add">${icon()}</md-filled-icon-button>`)+
 h('Floating action buttons — small / medium / large / extended')+p('fabs',['small','medium','large'].map(size=>`<md-fab variant="primary" size="${size}" aria-label="Add">${icon('icon')}</md-fab>`).join('')+`<md-fab variant="primary" label="Create">${icon('icon')}</md-fab>`)+
 h('Chips — assist / selected filter / removable input / suggestion')+p('chips',chips(false))+h('Disabled chips')+p('chips-disabled',chips(true)),
 inputs:h('Selection controls')+p('selection',controls(false))+h('Disabled selection controls')+p('selection-disabled',controls(true))+
 h('Sliders')+row(p('slider',slider('Continuous'))+p('slider-discrete',slider('Discrete','step="10" ticks labeled')))+row(p('slider-range',slider('Range','value-start="20" value-end="70" range','20 - 70')))+
 h('Text fields')+row(p('field-filled',field('filled'))+p('field-outlined',field('outlined')))+
 row(p('field-error',field('outlined','Error','error error-text="Error message"'))+p('field-password','<md-filled-text-field label="Password" value="password" type="password" supporting-text="Supporting text"></md-filled-text-field>'))+
 row(p('field-disabled',field('filled','Disabled','disabled'))+p('field-readonly',field('outlined','Read only','readonly'))),
 layout:h('Cards — filled / outlined / elevated (official experimental labs)')+p('cards',['filled','outlined','elevated'].map(v=>`<md-${v}-card>${v[0].toUpperCase()+v.slice(1)} card<br><span class="caption">Supporting text</span></md-${v}-card>`).join(''))+
 h('Primary tabs')+p('tabs-primary',tab('primary'))+h('Secondary tabs')+p('tabs-secondary',tab('secondary'))+
 h('Navigation bar (official experimental labs)')+p('navigation-bar','<md-navigation-bar>'+['Home','Activity','Settings'].map((v,i)=>`<md-navigation-tab ${i===0?'active':''} ${i===1?'show-badge badge-value="3"':''} label="${v}">${icon('active-icon',v[0])}${icon('inactive-icon',v[0])}</md-navigation-tab>`).join('')+'</md-navigation-bar>')+
 h('Lists — one, two, three lines + divider')+p('list',`<md-list><md-list-item>Headline${icon('start','A')}<span slot="end">100+</span></md-list-item><md-list-item><div slot="headline">Headline</div><div slot="supporting-text">Supporting text</div>${icon('start','B')}<span slot="end">24</span></md-list-item><md-list-item multiline-supporting-text><div slot="headline">Headline</div><div slot="supporting-text">Supporting text<br>Second supporting line</div>${icon('start','C')}<span slot="end">56</span></md-list-item><md-divider></md-divider></md-list>`),
 feedback:h('Progress — linear 40% / circular 75%')+row(p('progress-linear','<md-linear-progress value="0.4" aria-label="Loading"></md-linear-progress>')+p('progress-circular','<md-circular-progress value="0.75" aria-label="Loading"></md-circular-progress>'))+
 h('Segmented buttons — single / multiple (official experimental labs)')+row(p('segmented',segments(false))+p('segmented-multi',segments(true)))+
 h('Select — filled / outlined')+row(p('select-filled',select('filled'))+p('select-outlined',select('outlined')))+h('Badge (official experimental labs)')+p('badges','<md-icon-button aria-label="Add">'+icon()+'</md-icon-button><md-badge value="3"></md-badge>')+
 h('Menu')+'<div class="popup-anchor"><md-outlined-button id="menu-anchor">Open menu</md-outlined-button><md-menu anchor="menu-anchor" id="reference-menu" positioning="absolute" open><md-menu-item><div slot="headline">New item</div><span slot="end">Ctrl+N</span></md-menu-item><md-menu-item><div slot="headline">Show details</div><span slot="start">✓</span></md-menu-item><md-divider></md-divider><md-sub-menu><md-menu-item slot="item"><div slot="headline">More actions</div><span slot="end">›</span></md-menu-item><md-menu slot="menu"><md-menu-item><div slot="headline">Duplicate</div><span slot="end">Ctrl+D</span></md-menu-item><md-sub-menu><md-menu-item slot="item"><div slot="headline">Export</div><span slot="end">›</span></md-menu-item><md-menu slot="menu"><md-menu-item><div slot="headline">PNG image</div></md-menu-item><md-menu-item><div slot="headline">PDF document</div></md-menu-item></md-menu></md-sub-menu><md-menu-item disabled><div slot="headline">Unavailable</div></md-menu-item></md-menu></md-sub-menu><md-menu-item><div slot="headline">Delete</div></md-menu-item></md-menu></div>'
};
const params = new URLSearchParams(location.search);
const group = params.get('group') || 'actions';
const dark = params.get('theme') === 'dark';
document.documentElement.classList.toggle('dark', dark);
document.querySelector('#content').innerHTML = groups[group] || groups.actions;
document.querySelector('#title').textContent = `${group[0].toUpperCase()+group.slice(1)} / ${dark?'dark':'light'}`;
window.fixtureReady = true;
