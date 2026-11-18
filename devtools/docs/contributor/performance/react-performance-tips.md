# Writing efficient React code

In this article we'll discuss about the various component types we can use, as
well as discuss some tips to make your React application faster.

## TL;DR tips

* Prefer props and state immutability and use `PureComponent` components as a default
* As a convention, the object reference should change **if and only if** the inner data
  changes.
  * Be careful to never use new instance of functions as props to a Component (it's fine to use
    them as props to a DOM element).
  * Be careful to not update a reference if the inner data doesn't change.
* [Always measure before optimizing](./performance.md) to have a real impact on
  performance. And always measure _after_ optimizing too, to prove your change
  had a real impact.

## How React renders normal components

### What's a normal component?

As a start let's discuss about how React renders normal plain components, that
don't use `shouldComponentUpdate`. What we call plain components here are either:

* classes that extend [`Component`](https://reactjs.org/docs/react-component.html)

```jsx
  class Application extends React.Component {
    render() {
      return <div>{this.props.content}</div>;
    }
  }
```

* normal functions that take some `props` as parameter and return some JSX. We
  call these functions either Stateless Components or Functional Components.
  This is important to understand that these Stateless Components are _not_
  especially optimized in React.

```jsx
  function Application(props) {
    return <div>{props.content}</div>;
  }
```

  These functions are equivalent to classes extending `Component`. In
  the rest of the article we'll especially focus on the latter. Unless otherwise
  stated everything about classes extending `Component` is also true for
  Stateless/Functional Components.

#### Notes on the use of JSX

Because we don't use a build step in Firefox yet, some of our
tools don't use JSX and use [factories](https://reactjs.org/docs/react-api.html#createfactory)
instead:

```javascript
class Application extends React.Component {
  render() {
    return dom.div(null, this.props.content);
  }
}
```

We'll use JSX in this documentation for more clarity but this is strictly
equivalent. You can read more on [React documentation](https://reactjs.org/docs/react-without-jsx.html).

### The first render

There's only one way to start a React application and trigger a first render:
calling `ReactDOM.render`:

```javascript
ReactDOM.render(
  <Application content='Hello World!'/>,
  document.getElementById('root')
);
```

React will call that component's `render` method, and then recursively call
every child's `render` method, generating a rendering tree and then a virtual
DOM tree. It will then render actual DOM elements to the specified container.

### Subsequent rerenders

There are several ways to trigger a rerender:

1. We call `ReactDOM.render` again with the same component.

```javascript
  ReactDOM.render(
    <Application content='Good Bye, Cruel World!'/>,
    document.getElementById('root')
  );
```

2. One component's state changes, through the use of [`setState`](https://reactjs.org/docs/react-component.html#setstate).
   If the application is using Redux, this is how Redux-connected components
   trigger updates too.
3. One component's props change. But note that this can't happen by itself, this
   is always a consequence of the case 1 or 2 in one of its parents. So we'll
   ignore this case for this chapter.

When one of these happens, just like the initial render, React will call that
component's `render` method, and then recursively call every child's `render`
method, but this time possibly with changed props compared to the previous render.

These recursive calls produce a new rendering tree. That's where React uses an
algorithm called _virtual diffing_ or
[_reconciliation_](https://reactjs.org/docs/reconciliation.html) to find the
minimal set of updates to apply to the DOM. This is good because the less
updates to the DOM the less work the browser has to do to reflow and repaint the
application.

### Main sources of performance issues

From this explanation we can gather that the main performance issues can
come from:

1. triggering the render process **too frequently**,
2. **expensive** render methods,
3. the reconciliation algorithm itself. The algorithm is O(n) according to React
   authors, which means the processing duration increases linearly with **the number
   of elements in the tree** we compare. So a larger tree means a longer time to
   process.

Let's dive more into each one of these issues.

#### Do not render too often

A rerender will happen after calling `setState` to change the
local state.

Everything that's in the state should be used in `render`.
Anything in the state that's not used in `render` shouldn't be in the state, but
rather in an instance variable. This way you won't trigger an update if you
change some internal state that you don't want to reflect in the UI.

If you call `setState` from an event handler you may call it too often.
This is usually not a problem because React is smart enough to merge close
setState calls and trigger a rerender only once per frame. Yet if your `render`
is expensive (see below as well) this could lead to problems and you may want to
use `setTimeout` or other similar techniques to throttle the renders.

#### Keep `render` methods as lean as possible

When rendering a list, it's very common that we'll map this list to a list of
components. This can be costly and we might want to cut this list in several
chunks of items or to
[virtualize this list](https://reactjs.org/docs/optimizing-performance.html#virtualize-long-lists).
Although this is not always possible or easy.

Do not do heavy computations in your `render` methods. Rather do them before
setting the state, and set the state to the result of these computations.
Ideally `render` should be a direct mirror of the component's props and state.

Note that this rule also applies to the other methods called as part of the
rendering process: `componentWillUpdate` and `componentDidUpdate`. In
`componentDidUpdate` especially avoid synchronous reflows by getting DOM
measurements, and do not call `setState` as this would trigger yet another
update.

#### Help the reconciliation algorithm be efficient

The smaller the tree is, the faster the algorithm is. So it's
useful to limit the changes to a subtree of the full tree. Note that the use of
`shouldComponentUpdate` or `PureComponent` alleviates this issue by cutting off
entire branches from the rendering tree, [we discuss this in more details
below](#shouldcomponentupdate-and-purecomponent-avoiding-renders-altogether).

Try to change the state as close as possible to where your UI
should change (close in the components tree).

Do not forget to [set `key` attributes when rendering a list of
things](https://reactjs.org/docs/lists-and-keys.html), which shouldn't be the
array's indices but something that identifies the item in a predictable, unique
and stable way. This helps the algorithm
a lot by skipping parts that likely haven't changed.

### More documentation

The React documentation has [a very well documented page](https://reactjs.org/docs/implementation-notes.html#mounting-as-a-recursive-process)
explaining the whole render and rerender process.

## `shouldComponentUpdate` and `PureComponent`: avoiding renders altogether

React has an optimized algorithm to apply changes. But the fastest algorithm is
an algorithm that isn't executed at all.

[React's own documentation about performance](https://reactjs.org/docs/optimizing-performance.html#shouldcomponentupdate-in-action)
is quite complete on this subject.

### Avoiding rerenders with `shouldComponentUpdate`

As the first step of a rerender process, React calls your component's
[`shouldComponentUpdate`](https://reactjs.org/docs/react-component.html#shouldcomponentupdate)
method with 2 parameters: the new props, and the new
state. If this method returns false, then React will skip the render process for this
component, **and its whole subtree**.

```jsx
class ComplexPanel extends React.Component {
  // Note: this syntax, new but supported by Babel, automatically binds the
  // method with the object instance.
  onClick = () => {
    this.setState({ detailsOpen: true });
  }

  // Return false to avoid a render
  shouldComponentUpdate(nextProps, nextState) {
    // Note: this works only if `summary` and `content` are primitive data
    // (eg: string, number) or immutable data
    // (keep reading to know more about this)
    return nextProps.summary !== this.props.summary
      || nextProps.content !== this.props.content
      || nextState.detailsOpen !== this.state.detailsOpen;
  }

  render() {
    return (
      <div>
        <ComplexSummary summary={this.props.summary} onClick={this.onClick}/>
        {this.state.detailsOpen
          ? <ComplexContent content={this.props.content} />
          : null}
      </div>
    );
  }
}
```

**This is a very efficient way to improve your application speed**, because this
avoids everything: both calling render methods for this component _and_ the
whole subtree, and the reconciliation phase for this subtree.

Note that just like the `render` method, `shouldComponentUpdate` is called once
per render cycle, so it needs to be very lean and return as fast as possible. So
it should execute some cheap comparisons only.

### `PureComponent` and immutability

A very common implementation of `shouldComponentUpdate` is provided by React's
[`PureComponent`](https://reactjs.org/docs/react-api.html#reactpurecomponent):
it will shallowly check the new props and states for reference equality.

```jsx
class ComplexPanel extends React.PureComponent {
  // Note: this syntax, new but supported by Babel, automatically binds the
  // method with the object instance.
  onClick = () => {
    // Running this repeatidly won't render more than once.
    this.setState({ detailsOpen: true });
  }

  render() {
    return (
      <div>
        <ComplexSummary summary={this.props.summary} onClick={this.onClick}/>
        {this.state.detailsOpen
          ? <ComplexContent content={this.props.content} />
          : null}
      </div>
    );
  }
}
```

This has a very important consequence: for non-primitive props and states, that is
objects and arrays that can be mutated without changing the reference itself,
PureComponent's inherited `shouldComponentUpdate` will yield wrong results and will
skip renders where it shouldn't.

So you're left with one of these two options:

* either implement your own `shouldComponentUpdate` in a `Component`
* or (**preferred**) decide to make all your data structure immutable.

The latter is recommended because:

* It's much simpler to think about.
* It's much faster to check for equality in `shouldComponentUpdate` and in other
  places (like Redux' selectors).

Note you could technically implement your own `shouldComponentUpdate` in a
`PureComponent` but this is quite useless because `PureComponent` is nothing
more than `Component` with a default implementation for `shouldComponentUpdate`.

### About immutability

#### What it doesn't mean

It doesn't mean you need to enforce the immutability using a library like
[Immutable](https://github.com/facebook/immutable-js).

#### What it means

It means that once a structure exists, you don't mutate it.

**Every time some data changes, the object reference must change as well**. This
means a new object or a new array needs to be created. This gives the nice
reverse guarantee: if the object reference has changed, the data has changed.

It's good to go one step further to get a **strict equivalence**: if the data
doesn't change, the object reference mustn't change. This isn't necessary for
your app to work, but this is a lot better for performance as this avoids
spurious rerenders.

Keep reading to learn how to proceed.

#### Keep your state objects simple

Updating your immutable state objects can be difficult if the objects used are
complex. That's why it's a good idea to keep the objects simple, especially keep
them not nested, so that you don't need to use a library like
[immutability-helper](https://github.com/kolodny/immutability-helper),
[updeep](https://github.com/substantial/updeep), or even
[Immutable](https://github.com/facebook/immutable-js). Be especially careful
with Immutable as it's easy to create performance problems by misusing
its API.

If you're using Redux ([see below as well](#a-few-words-about-redux)) this
advice applies to your individual reducers as well, even if Redux tools make
it easy to have a nested/combined state.

#### How to update an object

Updating an object is quite easy.

You must not change/add/delete inner properties directly:

```javascript
// Note that in the following examples we use the callback version
// of `setState` everywhere, because we build the new state from
// the current state.

// Please don't do this as this will likely induce bugs.
this.setState(state => {
  state.stateObject.details = details;
  return state;
});

// This is wrong too: `stateObject` is still mutated.
this.setState(({ stateObject }) => {
  stateObject.details = details;
  return { stateObject };
});
```

Instead **you must create a new object** for this property. In this example
we'll use the object spread operator, already implemented in Firefox, Chrome and Babel.

However here we take care to return the same object if it doesn't need an update. The
comparison happens inside the callback because it depends on the state as
well. This is a good thing to do so that the shallow equality check doesn't
return false if nothing changes.

```javascript
// Updating one property in the state
this.setState(({ stateObject }) => ({
  stateObject: stateObject.content === newContent
    ? stateObject
    : { ...stateObject, content: newContent },
});

// This is very similar if 2 properties need an update:
this.setState(({ stateObject1, stateObject2 }) => ({
  stateObject1: stateObject1.content === newContent
    ? stateObject1
    : { ...stateObject1, content: newContent },
  stateObject2: stateObject2.details === newDetails
    ? stateObject2
    : { ...stateObject2, details: newDetails },
});

// Or if one of the properties needs to update 2 of it's own properties:
this.setState(({ stateObject }) => ({
  stateObject: stateObject.content === newContent && stateObject.details === newDetails
    ? stateObject
    : { ...stateObject, content: newContent, details: newDetails },
});
```

Note that this isn't about the returned `state` object, but its properties.
The returned object is always merged into the current state, and React creates
a new component's state object at each update cycle.

#### How to update an array

Updating an array is easy too.

You must avoid methods that mutate the array like push/splice/pop/shift and you
must not change directly an item.

```javascript
// Please don't do this as this will likely induce bugs.
this.setState(({ stateArray }) => {
  stateArray.push(newItem); // This is wrong
  stateArray[1] = newItem; // This is wrong too
  return { stateArray };
});
```

Instead here again you need to **create a new array instance**.

```javascript
// Adding an element is easy.
this.setState(({ stateArray }) => ({
  stateArray: [...stateArray, newElement],
}));

this.setState(({ stateArray }) => {
  // Removing an element is more involved.
  const newArray = stateArray.filter(element => element !== removeElement);
  // or
  const newArray = [...stateArray.slice(0, index), ...stateArray.slice(index + 1)];
  // or do what you want on a new clone:
  const newArray = stateArray.slice();
  return {
    // Because we want to keep the old array if removeElement isn't in the
    // filtered array, we compare the lengths.
    // We still start a render phase because we call `setState`, but thanks to
    // PureComponent's shouldComponentUpdate implementation we won't actually render.
    stateArray: newArray.length === stateArray.length ? stateArray : newArray,
  };

  // You can also return a falsy value to avoid the render cycle at all:
  return newArray.length === stateArray.length
    ? null
    : { stateArray: newArray };
});
```

#### How to update Maps and Sets

The process is very similar for Maps and Sets. Here is a quick example:

```javascript
// For a Set
this.setState(({ stateSet }) => {
  if (!stateSet.has(value)) {
    stateSet = new Set(stateSet);
    stateSet.add(value);
  }
  return { stateSet };
});

// For a Map
this.setState(({ stateMap }) => {
  if (stateMap.get(key) !== value) {
    stateMap = new Map(stateMap);
    stateMap.set(key, value);
  }
  return { stateMap };
}));
```

#### How to update primitive values

Obviously, with primitive types like boolean, number or string, that are
comparable with the operator `===`, it's much easier:

```javascript
this.setState({
  stateString: "new string",
  stateNumber: 42,
  stateBool: false,
});
```

Note that we don't use the callback version of `setState` here. That's because
for primitive values we don't need to use the previous state to generate a new
state.

#### A few words about Redux

When working with Redux, the rules stay the same, except all of this
happens in your reducers instead of in your components. With Redux comes the
function [`combineReducers`](https://redux.js.org/docs/api/combineReducers.html)
that obeys all the rules we outlined before while making it possible to have a
nested state.

### `shouldComponentUpdate` or `PureComponent`?

It is highly recommended to go the full **PureComponent + immutability** route,
instead of writing custom `shouldComponentUpdate` implementations for
components. This is more generic, more maintainable, less error-prone, faster.

Of course all rules have exceptions and you're free to implement a
`shouldComponentUpdate` method if you have specific cases to take care of.

### Some gotchas with `PureComponent`

Because `PureComponent` shallowly checks props and state, you need to take care
to not create a new reference for something that's otherwise identical. Some
common cases are:

* Using a new instance for a prop at each render cycle. Especially, do not use
  a bound function or an anonymous function (both classic functions or
  arrow functions) as a prop:

  ```javascript
  render() {
    return <MyComponent onUpdate={() => this.update()} />;
  }
  ```

  Each time the `render` method runs, a new function will be created, and in
  `MyComponent`'s `shouldComponentUpdate` the shallow check will always fail
  defeating its purpose.

* Using another reference for the same data. One very common example is the empty
  array: if you use a new `[]` for each render, you won't skip render. A solution
  is to reuse a common instance. Be careful as this can very well be hidden
  within some complicated Redux reducers.

* A similar issue can arise if you use sets or maps. If you add an element in a
  `Set` that's already in there, you don't need to return a new `Set` as it will be
  identical.

* Be careful with array's methods, especially `map` or `filter`, as they always
  return a new array. So even with the same inputs (same input array, same
  function), you'll get a new output, even if it contains the same data. If
  you're using Redux, [reselect](https://github.com/reactjs/reselect) is
  recommended.
  [memoize-immutable](https://github.com/memoize-immutable/memoize-immutable)
  can be useful in some cases too.

## Diagnosing performance issues with some tooling

[You can read about it in the dedicated
page](./performance.md).

## Breaking the rules: always measure first

You should generally follow these rules because they bring a consistent
performance in most cases.

However you may have specific cases that will need that you break the rules. In
that case the first thing to do is to **measure** using a profiler so that you
know where your problem are.

Then and only then you can decide to break the rules by using some mutable state
and/or custom `shouldComponentUpdate` implementation.

And remember to measure again after you did your changes, to check and prove
that your changes actually made an impact. Ideally you should always give links
to profiles when requesting a review for a performance patch.
