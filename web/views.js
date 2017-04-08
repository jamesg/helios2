/*
 * Return the first defined, non-null argument.  Call with any number of
 * arguments.
 *
 * If no arguments are defined and non-null, return null.
 */
var coalesce = function() {
    for(a in arguments)
        if(!_.isNull(arguments[a]) && !_.isUndefined(arguments[a]))
            return arguments[a];
    return null;
};

var StaticView = Backbone.View.extend(
    {
        initialize: function(options) {
            Backbone.View.prototype.initialize.apply(this, arguments);
            if(_.has(this, 'model') && !_.isUndefined(this.model))
                this.listenTo(this.model, 'change', this.render.bind(this));
            if(!_.isUndefined(options) && _(options).has('template'))
                this.template = options.template;
            if(!_.isUndefined(options) && _(options).has('templateParams'))
                this.templateParams = options.templateParams;
        },
        template: '',
        templateParams: function() {
            return (_(this).has('model') && !_.isUndefined(this.model))?
                this.model.toJSON():{};
        },
        render: function() {
            if(_.isFunction(this['template']))
                this.$el.html(this.template(this.templateParams()));
            else
                this.$el.html(_.template(this.template)(this.templateParams()));
        }
    }
);

var CollectionView = Backbone.View.extend(
    {
        initialize: function(options) {
            this.view = coalesce(
                options['view'],
                this['view'],
                function() { console.log('warning: view not defined'); }
                );
            this.filter = coalesce(
                options['filter'],
                this['filter'],
                function(model) { return true; }
                );
            this.constructView =
                coalesce(options['constructView'], this['constructView']);
            this.initializeView =
                coalesce(options['initializeView'], this['initializeView']);
            this.emptyView = coalesce(options['emptyView'], this['emptyView']);
            this._offset = coalesce(options['offset'], 0);
            this._limit = coalesce(options['limit'], -1);
            this._rendered = false;
            this._views = [];
            this.model.each(this.add, this);
            this.render();
            this.listenTo(this.model, 'add', this.add);//function() {
                //console.log('add event');
                //this.render();
            //});
            this.listenTo(this.model, 'remove', this.remove);
            this.listenTo(this.model, 'reset', this.reset);
            this.listenTo(
                    this.model,
                    'sync',
                    (function() {
                        console.log('sync', this.model.length);
                        this.render();
                    }).bind(this)
                    );
        },
        add: function(model, options) {
            var view = this.constructView(model);

            var pos = 0;
            // Increment pos until the next view in this._views has a greater
            // model index than the new view.
            while(
                pos < this._views.length &&
                this.model.indexOf(this._views[pos].model) < this.model.indexOf(model)
                )
                pos++;

            this._views.splice(pos, 0, view);

            //this.render();
            this.trigger('add');
        },
        remove: function(model) {
            var viewToRemove = this.get(model);
            this._views = _(this._views).without(viewToRemove);
            this.render();
            this.trigger('remove');
        },
        get: function(model) {
            return _(this._views).select(
                function(cv) { return cv.model === model; }
                )[0];
        },
        reset: function() {
            console.log('reset model');
            this._views = [];
            this._rendered = false;
            this.model.each(
                function(model) {
                    var view = this.constructView(model);
                    this._views.push(view);
                    },
                this
                );
            this.render();
            this.trigger('reset');
        },
        constructView: function(model) {
            var view = new this.view({ model: model });
            this.initializeView(view);
            view.render();
            return view;
        },
        initializeView: function(view) {
        },
        emptyView: StaticView.extend({}),
        each: function(f, context) {
            if(_.isUndefined(context))
                _(this._views).each(f);
            else
                _(this._views).each(f, context);
        },
        render: function() {
            // Replace the content of the element with a newly constructed view
            // for each model in the collection.
            //
            // TODO reuse old models, both for performance and to enable
            // editable views.
            this._rendered = true;
            var container = document.createDocumentFragment();

            var filtered = _.filter(
                    this._views,
                    (function(view) {
                        return this.filter(view.model);
                    }).bind(this),
                    this
                    );

            var views;
            if(this._limit >= 0)
                views = filtered.slice(this._offset, this._offset + this._limit);
            else
                views = filtered.slice(this._offset);

            if(views.length == 0) {
                if(!(_.has(this, '_emptyView'))) {
                    this._emptyView = new this.emptyView;
                    this._emptyView.render();
                }
                container.appendChild(this._emptyView.el);
            } else {
                if(_.has(this, '_emptyView')) {
                    this._emptyView.$el.remove();
                    delete this._emptyView;
                }
                _(views).each(
                    function(dv) {
                        container.appendChild(dv.el);
                    },
                    this
                    );
            }

            this.$el.empty();
            this.$el.append(container);

            // Rendering the CollectionView in this way removes
            // every element from the DOM and reinserts it
            // somewhere else.  Event bindings are lost.
            this.delegateEvents();
        },
        delegateEvents: function() {
            Backbone.View.prototype.delegateEvents.apply(this, arguments);
            _(this._views).each(function(dv) { dv.delegateEvents(); });
        }
    }
    );

/*
 * A generic table view that can be adapted to any kind of table with a header.
 * Provide a view constructor for the table header (thead) as the theadView
 * option and a view constructor for the table rows (tr) as the trView option.
 *
 * model: An instance of Backbone.Collection.
 * emptyView: View to use if the table is empty (defaults to displaying no
 * rows).
 * theadView: Header view constructor.
 * trView: Row view constructor.
 */
var TableView = StaticView.extend(
    {
        tagName: 'table',
        initialize: function(options) {
            StaticView.prototype.initialize.apply(this, arguments);

            this._thead = new options.theadView;
            this._thead.render();
            this._tbody = new CollectionView({
                tagName: 'tbody',
                view: options.trView,
                model: this.model
            });
            if(_.has(options, 'filter'))
                this._tbody.filter = options.filter;
            this._tbody.render();

            this.emptyView = coalesce(options['emptyView'], this['emptyView']);

            this.listenTo(this.model, 'all', this.render);

            this.render();
        },
        emptyView: StaticView,
        render: function() {
            this.$el.empty();
            this.$el.append(this._thead.$el);
            if(this.model.length == 0 && _.has(this, 'emptyView'))
                this.$el.append((new this.emptyView).$el);
            else if(this.model.length > 0)
                this.$el.append(this._tbody.$el);
            this._tbody.delegateEvents();
            this.delegateEvents();
        }
    }
    );

var CheckedCollectionView = CollectionView.extend(
        {
            // Get a list of models which are checked.
            checked: function() {
                var out = [];
                this.each(
                    function(view) {
                        if(view.$('input[type=checkbox]').prop('checked'))
                            out.push(view.model);
                    }
                );
                return out;
            },
            // Get a list of model ids which are checked.
            checkedIds: function() {
                return _.pluck(this.checked(), 'id');
            },
            // Set the list of checked models using a Backbone collection.
            setChecked: function(collection) {
                this.each(
                    function(view) {
                        view.$('input[type=checkbox]').prop(
                            'checked',
                            collection.some(
                                function(model) { return model.id == view.model.id }
                            )
                        );
                    }
                );
            },
            // Set the list of checked models using an array of ids.
            setCheckedIds: function(ids) {
                this.each(
                    function(view) {
                        view.$('input[type=checkbox]').prop(
                            'checked',
                            _.some(ids, function(id) { return id == view.model.id })
                        );
                    }
                );
            },
            checkAll: function() {
                this.each(
                    function(view) {
                        view.$('input[type=checkbox]').prop('checked', true);
                    }
                );
            },
            checkNone: function() {
                this.each(
                    function(view) {
                        view.$('input[type=checkbox]').prop('checked', false);
                    }
                );
            },
            invert: function() {
                this.each(
                    function(view) {
                        view.$('input[type=checkbox]').prop(
                            'checked',
                            !view.$('input[type=checkbox]').prop('checked')
                        );
                    }
                );
            }
        }
    );

